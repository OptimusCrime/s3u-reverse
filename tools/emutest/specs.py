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

import os
import struct
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
    # Runs this many times --cases (before max_cases): for specs whose cases
    # are spread over many independent paths (CGameData_UpdateSettlers: one
    # job family per case), so the default run reaches every block.
    case_scale: int = 1
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
    # undeclared slots are reported as spec errors. A third tuple element
    # holds options: "com" (a COM method: stdcall, ECX not recorded) and
    # "returns" (h -> EAX instead of 0 / 1 / random). A pointer argument's
    # bytes may also be ((offset, size), ...): only those parts are
    # recorded (for structures whose other fields are left uninitialized).
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
    # With new_fails: only allocations of this size fail (e.g. the exception
    # object, not the strings built before it).
    new_fails_size: int | None = None
    # Calls of S3.EXE functions that are recorded instead of run (see
    # emu.Emulator.set_callees): {address: (stack argument count,
    # "cdecl"/"stdcall"/"thiscall", "this"/"probe")}. Only for callees that
    # are not reimplemented and can't run in the emulator; say why at the
    # spec.
    callees: dict | None = None
    # Instruction limit per call instead of emu.MAX_INSTRUCTIONS (for
    # functions whose debug-build replacement needs more on big inputs).
    max_instructions: int | None = None
    # Quick runs (run.py --changed without --full): at most this many cases,
    # for specs too slow for per-batch runs whose main evidence is elsewhere
    # (say where at the spec). --full runs all.
    quick_cases: int | None = None
    # Peak memory of a worker running this spec, in MB, when far above the
    # usual (run.py WORKER_MEMORY_MB); lowers the default worker count.
    memory_mb: int | None = None


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


UI_FIELDS = {"CGameData_IsUiSelectionMode": 0x2d284, "CGameData_IsUiField298": 0x2d298,
             "CGameData_IsUiSelectionParam1": 0x2d288, "CGameData_IsUiSelectionParam2": 0x2d28c}


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


PILE_CLAIMS = 0x1195dd4


def pile_claim_slots(rng, pool, args):
    fields = []
    for slot in range(128):
        empty = rng.random() < 0.05
        fields += [(PILE_CLAIMS + slot * 4, 0 if empty else rng.randrange(8), 2),
                   (PILE_CLAIMS + slot * 4 + 2, 0 if empty else rng.randrange(8), 2)]
    fields.append((PILE_CLAIMS + rng.randrange(128) * 4, 0, 4))  # never full
    return fields


def pile_claims_cursor(rng, pool, args):
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
    Spec("CGameData_NextSearchGeneration", 0x004ff3f0, "thiscall", [], eax_mask=0,
         presets=small_map_and_generation),
    Spec("CGameData_InitStatisticsKernel", 0x004ff4c0, "thiscall", [], eax_mask=0),
    Spec("CGameData_ShiftStatistics", 0x004ff470, "thiscall", [], eax_mask=0,
         presets=one_active_player, max_cases=60),
    Spec("CGameData_AddStatistic", 0x004ff510, "thiscall", [below(20), below(36), anything],
         eax_mask=0),
    Spec("CGameData_SeedRandom_Impl", 0x004feb40, "thiscall", [anything], eax_mask=0),
    Spec("CGameData_Random", 0x004feb70, "thiscall", [], eax_mask=0xFFFF),
    # directions (map_directions.cpp)
    Spec("NeighbourX", 0x00503700, "stdcall", [anything, below(8, 0.2)]),
    Spec("NeighbourY", 0x00503730, "stdcall", [anything, below(8, 0.2)]),
    Spec("PreviousDirection", 0x005038a0, "stdcall", [below(8, 0.2)]),
    Spec("NextDirection", 0x005038c0, "stdcall", [below(8, 0.2)]),
    Spec("DirectionFromDelta", 0x005037f0, "stdcall", [between(-20, 20), between(-20, 20)]),
    # players (players.cpp)
    Spec("CGameData_AddPlayerCounter", 0x004ffb00, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("CGameData_SubtractPlayerCounter", 0x004ffb40, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("CGameData_ClearPlayerListLengths", 0x0050c9c0, "thiscall", []),
    # settlers (settlers.cpp)
    Spec("CGameData_CanSettlerEnterTile", 0x00503760, "thiscall",
         [below(384), below(384), below(20), below(48)], presets=full_map),
    Spec("CGameData_SettlerGroup", 0x005038e0, "thiscall", [below(4096)]),
    # map (map.cpp)
    Spec("CGameData_IsOnMap", 0x005194e0, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("CGameData_IsInsideMapBorder", 0x00519510, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("CGameData_TileScreenX", 0x00519610, "thiscall", [anything, anything],
         presets=map_size(anything)),
    Spec("CGameData_TileScreenY", 0x00519640, "thiscall", [below(384), below(384)]),
    # neighbourhood (map_neighbourhood.cpp)
    Spec("CGameData_ReplaceTerrainType", 0x00519210, "thiscall", [below(4), anything],
         presets=map_size(between(-1, 14))),
    Spec("CGameData_IsNeighbourhoodByte9Clear", 0x00519860, "thiscall", [TILE, TILE]),
    Spec("CGameData_HasAtMostTwoType70Neighbours", 0x005198d0, "thiscall", [TILE, TILE]),
    Spec("CGameData_IsNeighbourhoodTerrain", 0x0051b450, "thiscall", [TILE, TILE, below(8, 0.1)],
         presets=uniform_terrain),
    Spec("CGameData_AreOffsetTilesClear", 0x0051b3c0, "thiscall",
         [between(4, 379), between(4, 379), below(21)], presets=offset_tiles),
    Spec("CGameData_IsClearOfType70Nearby", 0x00519960, "thiscall", [TILE, TILE]),
    Spec("CGameData_IsFreeOfCatapultsNearby", 0x005199e0, "thiscall", [TILE, TILE],
         presets=full_map),
    # counting (counting.cpp)
    Spec("CGameData_CountBuildings", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountBuildingsWithFlag21Clear", 0x005158a0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountBuildingsWithWord8", 0x005159d0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountSettlers", 0x00515a30, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountSettlersWithJob", 0x00515a80, "thiscall", [below(5), below(5)],
         presets=record_counts),
    # building request flags (building_flags.cpp)
    *[Spec(f"CGameData_{kind}BuildingFlag{bit}", address, "thiscall", [between(1, 2000)],
           eax_mask=0, presets=building_request_state)
      for kind, bit, address in [
          ("Clear", "02", 0x004df490), ("Clear", "04", 0x004df510),
          ("Clear", "08", 0x004df590), ("Clear", "10", 0x004df610),
          ("Set", "02", 0x004df690), ("Set", "04", 0x004df730),
          ("Set", "08", 0x004df7d0), ("Set", "10", 0x004df850)]],
    # UI state queries (ui_state.cpp)
    *[Spec(name, address, "thiscall", [anything],
           presets=field_equals_arg_half_the_time(UI_FIELDS[name]))
      for name, address in [("CGameData_IsUiSelectionMode", 0x005157b0),
                            ("CGameData_IsUiField298", 0x005157d0),
                            ("CGameData_IsUiSelectionParam1", 0x005157f0),
                            ("CGameData_IsUiSelectionParam2", 0x00515810)]],
    Spec("CGameData_IsUiSelectionMode4Field298Is2", 0x00515830, "thiscall", [],
         presets=mode4_field298),
    Spec("CGameData_IsGameOutcome1", 0x00515ad0, "thiscall", [anything], globals=game_outcome),
    Spec("CGameData_ScreenCenterTileX", 0x005158f0, "thiscall", [], globals=screen_size),
    Spec("CGameData_ScreenCenterTileY", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("CGameData_ClampCamera", 0x0051b4d0, "thiscall", [], presets=camera,
         globals=screen_size),
    Spec("CGameData_PushUiFeedback", 0x004febe0, "thiscall", [anything],
         presets=ui_feedback_count),
    # free ids (id_allocators.cpp)
    Spec("CGameData_FindFreeId256", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("CGameData_FindFreeId4096", 0x0051d570, "thiscall", [],
         presets=id_table(0xe115e0, 0xe115e4, 0x1000), max_cases=400),
    Spec("CGameData_FindFreeId8192", 0x0051c040, "thiscall", [],
         presets=id_table(0xe125e4, 0xe125e8, 0x2000), max_cases=200),
    # map areas (map_areas.cpp)
    Spec("CGameData_AssignTileToArea", 0x004e5450, "thiscall",
         [below(384), below(384), below(AREA_LIMIT)]),
    Spec("CGameData_RemoveTileFromArea", 0x004e54a0, "thiscall", [below(384), below(384)],
         presets=tile_area_of_args),
    Spec("CGameData_SetTileAreaMarked", 0x005194b0, "thiscall",
         [below(384, 0.05), below(384, 0.05), anything]),
    Spec("CGameData_SetAreaBuildingSlotA", 0x004ddfe0, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    Spec("CGameData_SetAreaBuildingSlotB", 0x004de040, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    # map marks (map_marks.cpp)
    Spec("CGameData_SetDiagonalTileByteB", 0x00518d10, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=300),
    Spec("CGameData_ClearDiagonalTileByteB", 0x00518dc0, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=300),
    Spec("CGameData_ClearSightSlice", 0x00518ec0, "thiscall", [below(60, 0.1)],
         presets=slice_map_size, max_cases=300),
    # building/settler tile references (building_tiles.cpp)
    Spec("CGameData_RemoveBuildingFromTiles", 0x004d9940, "thiscall", [between(0, 2000)],
         eax_mask=0, presets=building_arg_on_map),
    Spec("CGameData_LeaveBuildingSlot", 0x0050cc10, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("CGameData_MoveSettlerToBuildingTileA", 0x0050de90, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("CGameData_MoveSettlerToBuildingTileB", 0x0050df40, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    # goods piles (goods_piles.cpp)
    Spec("CGameData_BuildingPileHasGoods", 0x004df9b0, "thiscall",
         [between(0, 2000), below(5, 0.15)], eax_mask=0xff, presets=building_pile_slots),
    Spec("CGameData_RecountPlayerGoods", 0x004ffa50, "thiscall", [], presets=goods_piles,
         max_cases=600),
    Spec("CGameData_PileNeighbourBuildingByteE", 0x00505fd0, "thiscall", [between(1, 3000)],
         presets=pile_neighbourhood),
    # tables, rings, timers (world_tables.cpp)
    Spec("CGameData_AllocBuildingExtra", 0x004df0a0, "thiscall", [anything],
         presets=alloc_table(0x10f45e8, 0x5a, 998, 0x110a578, 1, 0xff), max_cases=600),
    Spec("CGameData_AllocTable36700Record", 0x004fefd0, "thiscall", [anything] * 10,
         presets=alloc_table(0x36700, 20, 9998, 0x67440, 2, 0), max_cases=150),
    Spec("CGameData_QueueEvent", 0x004e8ad0, "thiscall",
         [event_type, below(4, 0.1), anything, anything], presets=event_rings),
    Spec("CGameData_QueueMarkedTile", 0x004e6c40, "thiscall",
         [below(384), below(384), below(4, 0.1)], eax_mask=0, presets=marked_tile),
    Spec("CGameData_QueueMarkedTileTerrain5To7", 0x0051ce50, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("CGameData_QueueMarkedTileNoOwner", 0x0051d3d0, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("CGameData_ReplaceTileWord16AndQueue", 0x0051c560, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x16, 2)),
    Spec("CGameData_ReplaceTileByte10AndQueue", 0x0051c820, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x10, 1)),
    Spec("CGameData_CountDownTimers", 0x004fee00, "thiscall", [], eax_mask=0, presets=timers,
         max_cases=200),
    Spec("CGameData_AddPileClaim", 0x00510770, "thiscall", [below(8, 0.1), below(8, 0.1)],
         eax_mask=0, presets=pile_claim_slots, globals=pile_claims_cursor, max_cases=800),
    # player queries (player_queries.cpp)
    Spec("CGameData_FindPlayerByField0C", 0x00507f10, "thiscall", [below(6, 0.1)],
         presets=players_field0c),
    Spec("BuildingTypeAllowedForRace", 0x004ffd60, "stdcall", [anything, building_type],
         eax_mask=0xff),
    # flood fills (flood_fills.cpp)
    Spec("CGameData_FloodFillTileWord16", 0x0051c6c0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x16, 2), max_cases=150),
    Spec("CGameData_FloodFillTileByte10", 0x0051c8a0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x10, 1), max_cases=150),
    Spec("CGameData_FloodMarkAreaTerrain5To7", 0x0051cee0, "thiscall",
         [below(12), below(12), anything], eax_mask=0, presets=flood_marked(True), max_cases=150),
    Spec("CGameData_FloodSetByte10MarkedTiles", 0x0051d450, "thiscall",
         [below(12), below(12), anything], eax_mask=0, presets=flood_marked(False), max_cases=150),
    Spec("CGameData_MoveTileToAreaAndQueue", 0x004e6430, "thiscall",
         [below(384), below(384), below(AREA_LIMIT, 0.1), below(AREA_LIMIT)],
         presets=area_of_args_matching),
    # terrain and borders (map_terrain.cpp)
    Spec("CGameData_ReplaceTerrainF3", 0x0051a400, "thiscall", [anything],
         presets=map_size(between(-1, 14))),
    Spec("CGameData_SpreadTerrainRandomly", 0x00519330, "thiscall",
         [below(4), below(4), anything, between(-5, 110)], presets=small_map_terrain,
         max_cases=600),
    Spec("CGameData_UpdateBorderFlagsAround", 0x0051a460, "thiscall", [below(16), below(16)],
         presets=territory),
    # camera, random, logic thread (ui_state.cpp, random.cpp)
    Spec("CGameData_CenterCameraOnTile", 0x0051b560, "thiscall", [below(384), below(384)],
         presets=camera_tile, globals=screen_size),
    Spec("CGameData_RandomBelow6", 0x00503880, "thiscall", []),
    Spec("CLogicThread_PushUiFeedback", 0x0053f560, "logicthread", [anything],
         presets=ui_feedback_count),
    # settlers (settler_queries.cpp)
    Spec("CGameData_SetSettlerTarget", 0x00503610, "thiscall",
         [below(0x10000), below(400, 0.1), below(400, 0.1)], presets=map_size(between(0, 400))),
    Spec("CGameData_CountListSettlersNear", 0x0050e8f0, "thiscall",
         [lambda rng, pool: 0 if rng.random() < 0.1 else rng.randrange(1, 0x3000),
          below(20, 0.2), below(384), below(384), below(3, 0.1)],
         presets=settler_list),
    Spec("CGameData_FindNearbyOwnCatapult", 0x0050cdb0, "thiscall",
         [between(1, 0x2fff), below(384), below(384)], presets=settlers_around),
    # game-end counts (game_end.cpp)
    Spec("CLogicThread_CountMilitaryBuildings", 0x00514a00, "logicthread", [scratch(0)],
         presets=game_end_buildings, max_cases=500),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514a90, "logicthread",
         [below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_CountTilesOwnedByEither", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_SumGoldPerPlayer", 0x00514b40, "logicthread", [scratch(0)],
         presets=area_piles),
    # map generation (map_generation.cpp)
    Spec("CGameData_ScatterTileByte11", 0x0051ad40, "thiscall", [anything, below(41)], eax_mask=0,
         presets=landscape(96), max_cases=300),
    Spec("CGameData_ScatterTerrainNear", 0x0051aed0, "thiscall",
         [one_of(0x20, 0x21, 0x80), anything, between(-3, 24)], eax_mask=0,
         presets=landscape(0), max_cases=300),
    Spec("CGameData_ScatterObjects", 0x0051b010, "thiscall",
         [one_of(0x20, 0x21, 0x80), anything, below(12), block_mode_extra, block_mode_extra],
         eax_mask=0, presets=landscape(20), max_cases=300),
    Spec("CGameData_ScatterObjectGroups", 0x0051b1a0, "thiscall",
         [one_of(0x20, 0x21, 0x80), below(8, 0.2), below(16, 0.2), below(48), block_mode_extra,
          block_mode_extra], eax_mask=0, presets=landscape(512), max_cases=300),
    # building input goods (building_goods.cpp)
    Spec("CGameData_BuildingInputWord2A", 0x004df150, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("CGameData_BuildingInputWord24", 0x004df220, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("CGameData_SetBuildingInputWord2A", 0x004df2f0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    Spec("CGameData_SetBuildingInputWord24", 0x004df3c0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    # god events, piles, presence (godly_events.cpp, area_piles.cpp, player_presence.cpp)
    Spec("CGameData_HasManaForSpell", 0x0054b240, "thiscall", [below(20), below(36, 0.1)],
         presets=mana),
    Spec("CGameData_AddTilePileToArea", 0x005051b0, "thiscall", [below(384), below(384)],
         presets=tile_pile),
    Spec("CGameData_IsOtherPlayerNear", 0x00510650, "thiscall",
         [below(400), below(400), below(40), below(20)], presets=presence, max_cases=400),
    # batch 14: region merges, area flood, requests, placement, terrain passes
    Spec("CGameData_FloodMoveArea", 0x004e62c0, "thiscall", [below(12), below(12), between(1, 6)],
         presets=flood_area, max_cases=150),
    Spec("CGameData_MergeTileWord16Regions", 0x0051c5e0, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, False), max_cases=500),
    Spec("CGameData_MergeTileByte10Regions", 0x0051ca00, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, True), max_cases=500),
    Spec("CGameData_AddBuildingGoodsRequest", 0x004ddf30, "thiscall",
         [below(2000), below(36, 0.1), one_of(0, 1, 7, 0xFFFFFFFF), below(64)], presets=goods_request),
    Spec("CGameData_RemoveBuildingFromAreaRequests", 0x004def80, "thiscall", [between(1, 63)],
         presets=request_lists, max_cases=300),
    Spec("CGameData_PlaceSettlerNear", 0x0050ce80, "thiscall",
         [below(0x3000), below(384, 0.03), below(384)], presets=place_settler, max_cases=400),
    Spec("CGameData_GrowTerrain", 0x00519470, "thiscall", [below(4), below(4), between(-5, 110)],
         presets=small_map_terrain, max_cases=600),
    Spec("CGameData_UpdateAllBorderFlags", 0x0051a420, "thiscall", [], presets=small_territory,
         max_cases=400),
    Spec("CGameData_CenterCameraOnLocalPlayer", 0x0051b6b0, "thiscall", [],
         presets=local_player_camera, globals=screen_size),
    # batch 15
    Spec("CGameData_AssignWord16RegionsAround", 0x0051c0f0, "thiscall", [between(3, 12), between(3, 12)],
         presets=region_assignment, max_cases=400),
    Spec("CGameData_SendNearestSettlerToPile", 0x004ddbd0, "thiscall",
         [between(1, 0x800), below(20), anything], presets=pile_and_waiting),
    Spec("CGameData_SendNearestSettlerToBuilding", 0x004ddd70, "thiscall", [between(1, 0x800)],
         presets=building_and_waiting),
    Spec("CGameData_RemoveSettlerFromLists", 0x0050dff0, "thiscall", [between(1, 8)],
         presets=settler_lists_world, globals=settler_lists_globals, max_cases=500),
    Spec("CGameData_AddGoodsToPile", 0x00505650, "thiscall", [between(1, 0xfff)], presets=pile_add),
    # batch 17
    Spec("CGameData_PileIncAmountLeaving", 0x005058c0, "thiscall", [between(1, 0xfff)], presets=pile_leaving,
         max_cases=1000),
    Spec("CGameData_LogPlayerLandResources", 0x00507f60, "thiscall", [below(20)], presets=player_on_map,
         max_cases=6),
    Spec("Landscape_FindOrAddColour", 0x0054c940, "thiscall", [colour_arg], presets=colour_table,
         max_cases=500),
    Spec("CLogicThread_DrawOverlayLine", 0x00523560, "logicthread", line_args(), presets=line_map,
         max_cases=500),
    Spec("CLogicThread_DrawWorkAreaOverlay", 0x00523840, "logicthread", [between(1, 0x100)],
         presets=work_area, max_cases=150),
    Spec("CGameData_SplitTileWord16Region", 0x0051c1f0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, 0.1)),
    Spec("CGameData_SplitTileByte10Region", 0x0051caf0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, 0.1)),
    Spec("CGameData_SplitWord16RegionsAround", 0x0051c080, "thiscall", [between(1, 10), between(1, 10)],
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, True), max_cases=200),
    Spec("CGameData_SendNearestWorkerToBuilding", 0x004db500, "thiscall",
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


# --- game-end check (game_end.cpp, victory_conditions.cpp, objectives.cpp,
# game_end_check.cpp), batch 46 -----------------------------------------------

GAME_TICK = 0x7dfd48
GAME_END_MISSION = 0x7dfdb0  # g_campaignMission
WORLD_UPDATE_DISABLED = 0x7deb55
PLAYER_TALLIES = 0x7acfd4
PLAYER_COLOUR_INDEX = 0x7ace64
LOCAL_PLAYER = 0x2d2d4
VICTORY_CONDITIONS = 0x15394b1
OFFSET_ENTRIES = 48
GAME_END_TICKS = (56250, 65625, 70312, 112500)
GAME_END_HARD_MISSIONS = (27, 30, 33, 34, 35, 36, 37, 39, 40, 41, 42, 43, 45, 47, 48, 51, 52, 54, 56, 58, 66, 68,
                          69, 72)

# Places the campaign missions look at (CheckGameEnd); the tiles around them
# get buildings, piles and settlers from the small test tables.
GAME_END_CENTRES = (
    (122, 112), (295, 194), (314, 208), (300, 222), (319, 249), (412, 504), (171, 326), (118, 541), (207, 358),
    (370, 335), (411, 266), (332, 74), (357, 292), (540, 47), (251, 74), (568, 387), (340, 576), (67, 319),
    (603, 34), (56, 607), (93, 43), (40, 250), (103, 381), (468, 506), (222, 342), (157, 394), (508, 463),
    (256, 420), (234, 425), (224, 438), (258, 444), (250, 456), (243, 540), (260, 551), (180, 483), (235, 497),
    (238, 542), (178, 434), (476, 37), (344, 430), (267, 536), (274, 522), (293, 508), (310, 510), (324, 511),
    (344, 519), (359, 527), (376, 533), (404, 554), (373, 548), (365, 588), (347, 595), (330, 599), (304, 592),
    (296, 576), (331, 571), (433, 538), (196, 67), (202, 53), (252, 54), (278, 70), (480, 474), (501, 494),
    (525, 480), (509, 466), (129, 90), (138, 95), (198, 138), (283, 193), (345, 209), (371, 230), (417, 218),
    (276, 149), (230, 100), (250, 70), (280, 100), (430, 320), (310, 220), (300, 180), (360, 180), (360, 140),
    (350, 100), (400, 100), (270, 20), (380, 20), (115, 175), (65, 150), (95, 120), (260, 290), (320, 290),
    (380, 290), (440, 290), (280, 340), (160, 250), (220, 250), (230, 160), (290, 160), (50, 50), (110, 50),
)
GAME_END_ARTS = (8, 15, 16, 18, 22, 25, 28, 34, 36, 41, 42, 43, 44, 45, 50)
GAME_END_SETTLER_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35, 0, 1, 36)


def small_owner(rng):
    return 0xff if rng.random() < 0.12 else rng.randrange(5)


def offset_rings(rng, entries=OFFSET_ENTRIES, wide=False):
    """The offset table: entries of growing (signed) distance, then -1 (the
    end for any radius below 0xffffffff)."""
    fields = []
    distance = rng.randint(-1, 30) if rng.random() < 0.1 else 10 if wide and rng.random() < 0.1 else 0
    for k in range(entries + 1):
        reach = 5 if wide else 3
        dx, dy = rng.randint(-reach, reach), rng.randint(-reach, reach)
        if rng.random() < 0.35:
            distance += 1
        stored = 0xFFFFFFFF if k == entries else distance & 0xFFFFFFFF
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, stored, 4)]
    return fields


def game_end_tile(rng, x, y, buildings=40, settlers=64, local=0):
    flags = rng.getrandbits(8) & ~0x21
    if rng.random() < 0.5:
        flags |= 0x20
    if rng.random() < 0.25:
        flags |= 0x01
    owner = local if rng.random() < 0.3 else small_owner(rng)
    return [(tile(x, y) + 0x8, owner, 1), (tile(x, y) + 0xa, flags, 1),
            (tile(x, y) + 0x2, 0 if rng.random() < 0.15 else rng.randint(1, buildings), 2),
            (tile(x, y) + 0x0, 0 if rng.random() < 0.3 else rng.randint(1, settlers), 2),
            (tile(x, y) + 0x6, 0x80 if rng.random() < 0.5 else rng.randrange(256), 1),
            (tile(x, y) + 0x10, rng.randrange(3), 1)]


def game_end_buildings_table(rng, count):
    fields = [(BUILDING_COUNT, count, 4)]
    for b in range(1, 41):
        record = BUILDINGS + b * BUILDING_BYTES
        art = rng.choice(GAME_END_ARTS) if rng.random() < 0.8 else rng.randrange(60)
        fields += [(record + 0x0, small_owner(rng), 1), (record + 0x6, art, 1),
                   (record + 0x21, 0 if rng.random() < 0.8 else rng.randint(1, 200), 1),
                   (record + 0x34, rng.randrange(64), 2)]
    return fields


def game_end_piles(rng):
    """Piles 1..40 (on tiles): a few goods and amounts."""
    fields = []
    for p in range(1, 41):
        good = rng.choice((8, 11, 8, 11, rng.randrange(36)))
        fields += [(PILES + p * PILE_BYTES, good, 1), (PILES + p * PILE_BYTES + 1, rng.randrange(30), 1)]
    return fields


def game_end_areas(rng, local):
    """Areas 1..10 with pile chains (piles from 100 on) for every good."""
    count = rng.choice([0, 1, rng.randrange(11), 10])
    fields = [(0xe115dc, count, 4)]
    next_pile = 100
    for area in range(1, 11):
        record = AREAS + area * AREA_BYTES
        owner = local if rng.random() < 0.4 else small_owner(rng)
        fields.append((record, owner, 1))
        for good in range(36):
            first = 0
            for _ in range(0 if rng.random() < 0.5 else rng.randrange(5)):
                fields += [(PILES + next_pile * PILE_BYTES + 0xc, first, 2),
                           (PILES + next_pile * PILE_BYTES + 0x1, rng.randrange(70), 1)]
                first = next_pile
                next_pile += 1
            fields.append((record + 0xa + good * 2, first, 2))
    return fields


def game_end_players(rng):
    fields = []
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(record + 0x0, 0 if rng.random() < 0.2 else rng.choice([1, rng.getrandbits(32) | 1]), 4),
                   (record + 0xc, rng.randrange(3), 4),
                   (record + 0x8c, rng.randint(150, 250), 4),
                   (record + 0x80, rng.randint(190, 300), 4),
                   (record + 0x12c, rng.randrange(400), 4),
                   (record + 0x134, 0 if rng.random() < 0.5 else rng.getrandbits(13), 4)]
    return fields


def victory_conditions(rng, local, places=GAME_END_CENTRES):
    """VictoryConditions (world+0x15394b1) with each condition on in a third
    of the cases; places on the map."""
    vc = VICTORY_CONDITIONS
    fields = [(vc + 0x00, int(rng.random() < 0.35), 1)]
    fields += [(vc + 0x01 + i, int(rng.random() < 0.15), 1) for i in range(20)]
    fields.append((vc + 0x15, int(rng.random() < 0.35), 1))
    for i in range(10):
        player = 0xff if rng.random() < 0.4 else rng.choice([rng.randrange(5), rng.randrange(5), 0xfe, 0x80])
        art = rng.choice(GAME_END_ARTS) if rng.random() < 0.8 else rng.choice([0x80, 0xff, rng.randrange(60)])
        fields += [(vc + 0x16 + i * 2, player, 1), (vc + 0x17 + i * 2, art, 1)]
    fields.append((vc + 0x2a, int(rng.random() < 0.35), 1))
    for i in range(5):
        x, y = rng.choice(places)
        fields += [(vc + 0x2b + i * 5, int(rng.random() < 0.5), 1), (vc + 0x2c + i * 5, x, 2),
                   (vc + 0x2e + i * 5, y, 2)]
    fields.append((vc + 0x44, int(rng.random() < 0.35), 1))
    for i in range(20):
        minutes = 0 if rng.random() < 0.8 else rng.choice([rng.randrange(4), rng.randrange(300), 0xffff, 40000])
        fields.append((vc + 0x45 + i * 2, minutes, 2))
    fields.append((vc + 0x6d, int(rng.random() < 0.35), 1))
    for i in range(3):
        fields += [(vc + 0x6e + i * 3, 0 if rng.random() < 0.3 else rng.randrange(60), 2),
                   (vc + 0x70 + i * 3, rng.randrange(36) if rng.random() < 0.9 else rng.choice([0xff, 0x80]), 1)]
    return fields


def game_end_settlers(rng, local):
    count = rng.choice([0, 1, rng.randrange(65), 64])
    fields = [(SETTLER_COUNT, count, 4)]
    for s in range(0, 66):
        owner = local if rng.random() < 0.3 else small_owner(rng)
        fields += [(SETTLERS + s * SETTLER_BYTES, owner, 1),
                   (SETTLERS + s * SETTLER_BYTES + 1, rng.choice(GAME_END_SETTLER_TYPES), 1)]
    return fields


# Buildings the missions look for (CheckGameEnd: IsBuildingNear goals and the
# tower places of missions 42/45/69, the marketplaces of 34/58): (x, y, art,
# owner) with owner "local", "ally" (another player of the local team),
# "any" or a player number.
GAME_END_GOALS = (
    (332, 74, 44, "local"), (357, 292, 18, "local"), (540, 47, 25, "local"), (251, 74, 15, "local"),
    (568, 387, 15, "local"), (340, 576, 15, "local"), (67, 319, 15, "local"), (603, 34, 41, "local"),
    (56, 607, 41, "local"), (468, 506, 45, "local"), (508, 463, 45, "any"), (256, 420, 41, 3), (234, 425, 42, 3),
    (224, 438, 42, 3), (258, 444, 42, 3), (250, 456, 42, 3), (243, 540, 50, "any"), (260, 551, 50, "any"),
    (180, 483, 15, 0), (235, 497, 15, 0), (238, 542, 15, 0), (267, 536, 18, 0), (274, 522, 16, 0),
    (293, 508, 16, 0), (310, 510, 16, 0), (324, 511, 16, 0), (344, 519, 15, 0), (359, 527, 16, 0),
    (376, 533, 16, 0), (404, 554, 16, 0), (373, 548, 16, 0), (365, 588, 16, 0), (347, 595, 16, 0),
    (330, 599, 16, 0), (304, 592, 16, 0), (296, 576, 16, 0), (331, 571, 15, 0), (433, 538, 16, 0),
    (196, 67, 15, 0), (202, 53, 16, 0), (252, 54, 15, 0), (278, 70, 16, 0), (480, 474, 15, 0), (501, 494, 16, 0),
    (525, 480, 15, 0), (509, 466, 16, 0), (129, 90, 15, 0), (138, 95, 16, 0), (412, 504, 34, "ally"),
    (171, 326, 34, "ally"), (118, 541, 43, "any"),
)
GOAL_BUILDINGS = 41  # goal k uses building 41 + k (outside the counted 1..40)


def game_end_world(rng, pool, args):
    local = rng.randrange(4)
    ally = (local + 1) % 4
    # The last check: random (in time), or just before the ticks game_end_globals favours (too early).
    last = rng.getrandbits(32) if rng.random() < 0.8 else rng.choice(GAME_END_TICKS) - rng.randrange(20, 30)
    scenario = rng.choice(("random", "random", "army", "army", "peace", "goals"))
    fields = [(MAP_SIZE, 768 if rng.random() < 0.9 else rng.randrange(300, 768), 4),
              (0x1195dcc, int(rng.random() < 0.15), 1), (0x2d248, rng.randrange(150), 4),
              (LOGIC_THREAD + 0x24c, last & 0xFFFFFFFF, 4)]
    fields += [(0x1195dcd + k, rng.randrange(36), 1) for k in range(7)]
    fields += game_end_players(rng)
    if scenario == "peace":
        local = 0
    fields += [(LOCAL_PLAYER, local, 4)]
    team = rng.randrange(3)
    fields += [(PLAYER_RECORDS + local * PLAYER_RECORD_BYTES + 0xc, team, 4),
               (PLAYER_RECORDS + ally * PLAYER_RECORD_BYTES + 0xc, team, 4)]
    # Buildings 1..40 (counted by CountMilitaryBuildings, up to the count);
    # 1..5 mostly military buildings of players 0..4.
    count = rng.choice([0, rng.randrange(41), 40, 40])
    fields += game_end_buildings_table(rng, count)
    for player in range(5):
        record = BUILDINGS + (player + 1) * BUILDING_BYTES
        military = (rng.random() < 0.75 or (scenario == "peace" and player == 0)) and \
            (scenario not in ("peace", "goals") or player not in (2, 3)) and (scenario != "peace" or player == 0)
        fields += [(record + 0x0, player, 1), (record + 0x21, 0, 1),
                   (record + 0x6, rng.choice((15, 16, 18)) if military else rng.choice((8, 22, 28, 36, 41, 42)), 1)]
    if scenario == "peace":  # nobody but player 0 has military buildings
        for b in range(6, 41):
            fields.append((BUILDINGS + b * BUILDING_BYTES, rng.choice((0, 0, 0xff)), 1))
    elif scenario == "goals":  # the buildings mission 34 wants, of the local player; a gold mine of player 1
        for b, art, owner in zip(range(6, 13), (41, 42, 28, 41, 42, 28, 8), (local,) * 6 + (1,)):
            fields += [(BUILDINGS + b * BUILDING_BYTES, owner, 1), (BUILDINGS + b * BUILDING_BYTES + 0x6, art, 1),
                       (BUILDINGS + b * BUILDING_BYTES + 0x21, 0, 1)]
    fields += game_end_settlers(rng, local)
    army = scenario == "army" or (scenario == "peace" and rng.random() < 0.5)
    if army:  # 130 soldiers of one player (missions 27, 35, 37, 47, 51, 54)
        owner = rng.choice((local, local, 1, 3))
        fields.append((SETTLER_COUNT, 130, 4))
        for s in range(0, 132):
            fields += [(SETTLERS + s * SETTLER_BYTES, owner, 1),
                       (SETTLERS + s * SETTLER_BYTES + 1, rng.choice((33, 34, 35, 33, 34, 35, 5, 8, 15, 30)), 1)]
    fields += game_end_piles(rng)
    fields += game_end_areas(rng, 1 if scenario == "goals" and rng.random() < 0.5 else local)
    fields += offset_rings(rng, wide=True)
    if rng.random() < 0.7:  # the place itself first
        fields += [(NEIGHBOUR_OFFSETS, 0, 4), (NEIGHBOUR_OFFSETS + 4, 0, 4), (NEIGHBOUR_OFFSETS + 8, 0, 4)]
    fields += victory_conditions(rng, local)
    reach = 5 if army else 4
    for cx, cy in GAME_END_CENTRES:
        for dx in range(-reach, reach + 1):
            for dy in range(-reach, reach + 1):
                if (dx or dy) and not army and rng.random() < 0.6:
                    continue
                fields += game_end_tile(rng, cx + dx, cy + dy, settlers=130 if army else 64,
                                        local=local)
                if army:
                    fields.append((tile(cx + dx, cy + dy), rng.randint(1, 130), 2))
    every_goal = scenario == "goals" or rng.random() < 0.3
    for k, (x, y, art, owner) in enumerate(GAME_END_GOALS):
        if not every_goal and rng.random() < 0.5:
            continue
        building = GOAL_BUILDINGS + k
        record = BUILDINGS + building * BUILDING_BYTES
        player = {"local": local, "ally": ally, "any": rng.randrange(5)}.get(owner, owner)
        exact = scenario == "goals"
        if not exact and rng.random() < 0.1:
            player = small_owner(rng)
        fields += [(tile(x, y) + 0x2, building, 2), (tile(x, y) + 0xa, 0x20 | (rng.getrandbits(8) & 0xde), 1),
                   (tile(x, y) + 0x8, player, 1), (record + 0x0, player, 1), (record + 0x6, art, 1),
                   (record + 0x21, 0 if exact or rng.random() < 0.95 else 1, 1), (record + 0x34, rng.randrange(64), 2)]
    if rng.random() < 0.5:  # all places of the "own every place" goals held
        owner = local if rng.random() < 0.5 else 0
        for x, y in GAME_END_CENTRES:
            fields.append((tile(x, y) + 0x8, owner, 1))
    if rng.random() < 0.3:  # mission 33's places held by players 1 and 2
        for x, y in ((295, 194), (314, 208), (300, 222), (319, 249)):
            fields.append((tile(x, y) + 0x8, rng.choice((1, 2)), 1))
    return fields

def game_end_globals(rng, pool, args):
    tick = rng.randrange(130000) if rng.random() < 0.5 else rng.choice(GAME_END_TICKS)
    if rng.random() < 0.3:
        tick += rng.randint(-2, 2)
    # Half the cases go to the missions whose goals need rare worlds.
    mission = rng.randrange(101) if rng.random() < 0.5 else rng.choice(GAME_END_HARD_MISSIONS)
    if rng.random() < 0.03:
        mission = rng.getrandbits(32)
    outcome = 0xFFFFFFFF if rng.random() < 0.95 else rng.choice([0, 1, 2])
    fields = [(GAME_TICK, tick, 4), (GAME_END_MISSION, mission, 4), (GAME_OUTCOME, outcome, 4),
              (WORLD_UPDATE_DISABLED, 0, 1)]
    for p in range(20):
        fields.append((PLAYER_TALLIES + p * 0x44 + 0xc, rng.choice([rng.randint(450, 650), rng.getrandbits(32)]), 4))
        fields.append((PLAYER_COLOUR_INDEX + p * 2, rng.randrange(8), 2))
    return fields



def victory_world(rng, pool, args):
    local = rng.randrange(4)
    fields = [(MAP_SIZE, 768, 4), (LOCAL_PLAYER, local, 4)]
    fields += game_end_players(rng)
    fields += game_end_buildings_table(rng, rng.choice([0, rng.randrange(41), 40]))
    fields += game_end_areas(rng, local)
    fields += victory_conditions(rng, local, places=((10, 10), (11, 12), (20, 5), (7, 30)))
    for x, y in ((10, 10), (11, 12), (20, 5), (7, 30)):
        fields.append((tile(x, y) + 0x8, local if rng.random() < 0.6 else small_owner(rng), 1))
    return fields


def victory_globals(rng, pool, args):
    tick = rng.randrange(40 * 938) if rng.random() < 0.7 else rng.getrandbits(32)
    fields = [(GAME_TICK, tick, 4), (GAME_END_MISSION, 0 if rng.random() < 0.5 else rng.randint(1, 72), 4)]
    fields += [(PLAYER_COLOUR_INDEX + p * 2, rng.randrange(8), 2) for p in range(20)]
    return fields


def ngo_world(rng, pool, args):
    fields = [(LOCAL_PLAYER, rng.randrange(20), 4)]
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(record, 0 if rng.random() < 0.3 else rng.getrandbits(32) | 1, 4), (record + 0xc, rng.randrange(3), 4)]
        for good in range(36):
            fields.append((record + 0xdc + good * 2, rng.randrange(4) if rng.random() < 0.8 else rng.getrandbits(16), 2))
    fields += [(0x1195dcd + k, rng.randrange(36) if rng.random() < 0.9 else rng.randrange(256), 1) for k in range(7)]
    return fields


def colour_players(rng, pool, args):
    return [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES, 0 if rng.random() < 0.3 else 1, 4) for p in range(20)]


def colour_table(rng, pool, args):
    return [(PLAYER_COLOUR_INDEX + p * 2, rng.randrange(24), 2) for p in range(20)]


def neighbourhood(rng, pool, args):
    """IsBuildingNear / SumGoodsNear: offsets, and tiles around (args[0],
    args[1]) with buildings 1..40 / piles 1..40."""
    x, y = args[0], args[1]
    fields = [(MAP_SIZE, 400 if rng.random() < 0.85 else rng.randrange(401), 4)]
    fields += offset_rings(rng, entries=36)
    fields += game_end_buildings_table(rng, 40)[1:]
    fields += game_end_piles(rng)
    for dx in range(-3, 4):
        for dy in range(-3, 4):
            if 0 <= x + dx < 400 and 0 <= y + dy < 400:
                fields += game_end_tile(rng, x + dx, y + dy)
    return fields


def game_end_player_arg(rng, pool):
    return rng.choice([0xFFFFFFFF, 0xFFFFFFFE, 0x80000000, rng.randrange(5), rng.randrange(5)])


SPECS += [
    Spec("CLogicThread_IsBuildingNear", 0x00514bf0, "logicthread",
         [between(3, 396), between(3, 396), lambda rng, pool: rng.choice([0, 1, 2, 3, 5, 0xFFFFFFFE, 0x80000000]),
          lambda rng, pool: rng.choice(GAME_END_ARTS + (rng.getrandbits(32),)), game_end_player_arg],
         eax_mask=0xFF, presets=neighbourhood, max_cases=800),
    Spec("CLogicThread_SumGoodsNear", 0x00514cc0, "logicthread",
         [between(3, 396), between(3, 396), lambda rng, pool: rng.choice([0, 1, 2, 3, 5, 0xFFFFFFFE, 0x80000000]),
          lambda rng, pool: rng.choice((8, 11, rng.getrandbits(32))), game_end_player_arg],
         presets=neighbourhood, max_cases=800),
    Spec("CGameData_DecideNgoOutcome", 0x004ffc80, "thiscall", [], presets=ngo_world, max_cases=800),
    Spec("CGameData_PlayerOfColour", 0x004ffe00, "thiscall",
         [lambda rng, pool: rng.choice([rng.randrange(24), rng.getrandbits(32)])],
         presets=colour_players, globals=colour_table),
    Spec("CLogicThread_CheckVictoryConditions", 0x005145c0, "logicthread", [], presets=victory_world,
         globals=victory_globals, max_cases=1000),
    # The jump table of the 100 missions follows the code at 0x514424 (data, not code).
    Spec("CLogicThread_CheckGameEnd", 0x00511c50, "logicthread", [], eax_mask=0xFF, presets=game_end_world,
         globals=game_end_globals, cover_until=0x00514424, case_scale=5),
]

SPECS += [
    Spec("CGameData_UnblockTile", 0x0051b770, "thiscall", [between(3, 8), between(3, 8)], presets=unblock_tile,
         max_cases=300),
    Spec("CGameData_SetOwnerOfFreeTile", 0x0051b5d0, "thiscall",
         [between(1, 8), between(1, 8), lambda rng, pool: rng.choice([0, 1, 2, 0xff, 0x100, rng.getrandbits(32)])],
         presets=free_tile_owner, max_cases=400),
    Spec("CGameData_MergeAreaContents", 0x004e5a20, "thiscall", two_areas(), eax_mask=0,
         presets=merge_area_contents, max_cases=300),
    Spec("CGameData_JoinAreasAtTile", 0x004e6150, "thiscall", [between(1, 8), between(1, 8)], eax_mask=0,
         presets=join_areas, max_cases=400),
    Spec("CGameData_QueuePendingEvent", 0x004e8ba0, "thiscall", [pending_type, below(4, 0.1), anything],
         presets=pending_rings),
    Spec("CLogicThread_DrawBuildingRouteOverlay", 0x005236b0, "logicthread", [between(1, 0x100)], eax_mask=0,
         presets=building_route, max_cases=400),
    Spec("CGameData_FetchGoodsToTradeBuilding", 0x004de0a0, "thiscall",
         [between(1, 0x40), between(1, 0x800), below(36)], eax_mask=0, presets=fetch_goods, max_cases=1000),
    Spec("CGameData_InitArea", 0x004e6510, "thiscall",
         [between(1, 0xffe), lambda rng, pool: rng.choice([rng.randrange(20), rng.randrange(256)])], eax_mask=0,
         presets=init_area, max_cases=500),
    Spec("CGameData_AllocArea", 0x004e64c0, "thiscall", [below(20)], presets=alloc_area, max_cases=300),
    Spec("CGameData_MoveAreaContents", 0x004e54f0, "thiscall", two_areas(), eax_mask=0,
         presets=move_area_contents, max_cases=300),
    Spec("CGameData_SplitAreaAtTile", 0x004e5d50, "thiscall", [below(10), below(10)], eax_mask=0,
         presets=split_area, max_cases=400),
    Spec("CGameData_BlockTile", 0x0051b6f0, "thiscall", [below(10), below(10)], presets=block_tile,
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


def castle_buildings(rng, pool, args):
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
    Spec("CGameData_SettlerStartHealth", 0x00510470, "thiscall", [below(LAST_SETTLER + 1)], presets=settler_health),
    Spec("CGameData_CreateSettler", 0x0050c9e0, "thiscall",
         [below(384), below(384), lambda rng, pool: rng.randrange(0x50) if rng.random() < 0.8 else anything(rng, pool),
          below(20)], presets=create_settler, max_cases=300),
    Spec("CGameData_ClearSettlerJob", 0x00510440, "thiscall", [below(LAST_SETTLER + 1)]),
    Spec("CGameData_RestoreSettlerAnimation", 0x0050e380, "thiscall", [below(LAST_SETTLER + 1)]),
    Spec("CGameData_PileIncAmountComing", 0x00505bc0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("CGameData_PileDecAmountComing", 0x00505bf0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("CGameData_RemovePileFromTile", 0x00504650, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("CGameData_ClearAreaGoodsSettings1D2", 0x004ff3c0, "thiscall", [below(0x1000)]),
    Spec("CGameData_FloodAssignMarkedTiles", 0x004e6cd0, "thiscall",
         [below(12), below(12), between(1, 6), below(3)], presets=flood_assign, max_cases=150),
    Spec("CGameData_TakeBuildingExtraTiles", 0x004dd6f0, "thiscall", [between(1, 64)], presets=take_extra_tiles,
         max_cases=300),
    Spec("CGameData_ReleaseBuildingExtraTiles", 0x004dda10, "thiscall", [between(1, 64)],
         presets=release_extra_tiles, max_cases=300),
    Spec("CGameData_UnblockCastleTiles", 0x004dfa60, "thiscall", [], presets=castle_buildings,
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
    Spec("CGameData_IsNgoObjectiveChosen", 0x004ffb90, "thiscall", [ngo_id], eax_mask=0xFF, presets=ngo_objectives),
    Spec("CGameData_GoodsRequestPriority", 0x004ff380, "thiscall", [anything, anything]),
    Spec("CGameData_IsSettlerJob01Or37", 0x004fa6b0, "thiscall", [job_value]),
    Spec("CGameData_JobAfter01Or37", 0x004faa40, "thiscall", [job_value]),
    Spec("CGameData_CountAreaSettlersOfClass0", 0x0050e750, "thiscall", [below(4)], presets=area_settlers,
         max_cases=500),
    Spec("CGameData_CountAreaSettlersOfType", 0x0050e820, "thiscall", [below(4), area_settler_type],
         presets=area_settlers, max_cases=500),
    Spec("CGameData_PileDecAmountLeaving", 0x00505ac0, "thiscall", [between(1, 0xfff)], presets=linked_pile),
    Spec("CGameData_MakePileEmpty", 0x00506080, "thiscall", [between(1, 0xfff)], presets=linked_pile),
    Spec("CGameData_ReplaceTerrainNextTo", 0x00519270, "thiscall", [terrain_value, terrain_value, anything],
         eax_mask=0, presets=small_terrain_map, max_cases=300),
    Spec("CGameData_TileShade", 0x00519720, "thiscall", [between(1, 380), between(1, 380)], presets=shade_heights),
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
    # Batch 27: the byte-identical twin in Dirk\GameParams.cpp's unit.
    Spec("String_ConcatChars2", 0x00437480, "thiscall", [text_or_null], presets=string_world,
         edx=one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), exempt_blocks=(0x004374f6,)),
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
    Spec("CGraphicsData_FrameCount", 0x005fad20, "thiscall",
         [lambda rng, pool: (rng.randrange(0x100) << 16) | rng.randrange(0x100)],
         presets=sprite_tables),
    Spec("GfxDword_Construct", 0x005fc540, "thiscall", [anything]),
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
    Spec("CGameData_CreatePile", 0x00504420, "thiscall",
         [below(384), below(384), pile_goods, pile_amount, pile_kind], presets=create_pile_state,
         max_cases=800),
    Spec("CLogicThread_UpdateGroupMenu", 0x00542990, "logicthread", [below(10)],
         presets=group_menu_presets, globals=group_menu_globals),
]

# -- batch 25: DirectX surface blits and error checks (directx_surface.cpp) --
#
# ECX = the world as a CSurface: +8 its COM interface (UI_BASE + 0x500,
# vtable = emu.FAKE_VTABLE: Blt slot 0x14 and BltFast slot 0x1c are COM
# probes returning one of the HRESULTs below), +0xc its owner (UI_BASE +
# 0x540, slot 0x0c = restore); a source CSurface at UI_BASE + 0x580 (its +8
# another interface). Callee probes: the exception constructors 0x5be180
# (CDirectDrawException) and 0x5bfdb0 (CDirectPlayException) build objects
# with strings, std::string and the runtime-error base, none reimplemented
# yet; they are recorded with their arguments (and return `this`).

DD_OK = 0
DDERR_SURFACELOST = 0x887601C2
DDERR_WASSTILLDRAWING = 0x8876021C
SURFACE_IFACE = UI_BASE + 0x500
SURFACE_OWNER = UI_BASE + 0x540
SOURCE_SURFACE = UI_BASE + 0x580
SOURCE_IFACE = UI_BASE + 0x5c0
EXCEPTION_CTORS = {0x005be180: (3, "thiscall", "this"), 0x005bfdb0: (3, "thiscall", "this")}


def hresult_from(h: int) -> int:
    """DD_OK most often (ends the retry loops), the two handled errors, a
    success code, other failures."""
    choice = h % 8
    if choice < 3:
        return DD_OK
    return [DDERR_WASSTILLDRAWING, DDERR_SURFACELOST, 1 + ((h >> 8) & 0xFF),
            0x88760000 | ((h >> 8) & 0xFFFF), (h >> 8) & 0xFFFFFFFF][choice - 3]


def hresult(rng, pool) -> int:
    return hresult_from(rng.getrandbits(40))


def surface_objects(rng, pool, args):
    fields = [(8, SURFACE_IFACE, 4), (0xc, SURFACE_OWNER, 4),
              (SURFACE_IFACE - WORLD_BASE, FAKE_VTABLE, 4), (SURFACE_OWNER - WORLD_BASE, FAKE_VTABLE, 4),
              (SOURCE_SURFACE - WORLD_BASE + 8, SOURCE_IFACE, 4)]
    return fields


SURFACE_VIRTUALS = {0x14: (6, {}, {"com": True, "returns": hresult_from}),
                    0x1c: (6, {}, {"com": True, "returns": hresult_from}),
                    0x0c: 0}


def surface_spec(name, address, args, **kwargs):
    return Spec(name, address, "thiscall", args, presets=surface_objects, virtuals=SURFACE_VIRTUALS,
                callees=EXCEPTION_CTORS, new_fails=0.2, **kwargs)


SPECS += [
    Spec("DirectX_ThrowIfDDFailed", 0x00405d10, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS,
         new_fails=0.2),
    Spec("DirectX_ThrowIfDPFailed", 0x005bb890, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS,
         new_fails=0.2),
    surface_spec("DirectX_CSurface_CheckResult", 0x005bf2e0, [hresult, anything]),
    surface_spec("DirectX_CSurface_DDBlt", 0x005bf0c0,
                 [anything, one_of(0, SOURCE_SURFACE), anything, anything, anything]),
    surface_spec("DirectX_CSurface_DDBltFast", 0x005bf130,
                 [anything, anything, one_of(SOURCE_SURFACE), anything, anything]),
]

# -- batch 25: background fill and "enabled changed" (screen_element.cpp) ---
#
# The fill (vf11) gets a surface: the world as a CSurface (surface_objects
# above, in the same world as the UI objects at UI_BASE). DDBlt (ours) runs
# inside; its Blt probe records the two DDBLTFX fields that are set (dwSize,
# dwFillColor at +0x50); the original leaves the rest uninitialized.

FILL_VIRTUALS = dict(UI_VIRTUALS)
FILL_VIRTUALS[0x14] = (6, {5: ((0, 4), (0x50, 4))}, {"com": True, "returns": hresult_from})
FILL_VIRTUALS[0x0c] = 0  # (also the owner's restore slot)


def fill_objects(rng, pool, args):
    return ui_objects(rng, pool, args) + surface_objects(rng, pool, args)


def fill_spec(name, address, this):
    return Spec(name, address, "fastcall", [one_of(this), anything, one_of(WORLD_BASE), one_of(0, UI_RECTS)],
                presets=fill_objects, virtuals=FILL_VIRTUALS, callees=EXCEPTION_CTORS, eax_mask=0xFF)


GROUP_EFFECTS[0x6c] = shrink_children

# Dead inlined-bounds-check blocks of 0x4177f0's loop (EMPTY: the
# "vector is empty" branch and its grow-to-one code, right after the loop's
# index < length check with no call in between; NEG: the index < 0 branch;
# see batch 24 above).
EMPTY_4177F0 = (0x417825, 0x41783a, 0x417842, 0x417847, 0x417857, 0x417862, 0x417868, 0x41786a, 0x41786e,
                0x417876, 0x41787e, 0x41789f, 0x4178b2, 0x4178b7, 0x4178cb)

SPECS += [
    fill_spec("CScreenElement_FillBackground", 0x00415b60, ELEMENT),
    fill_spec("CScreenGroup_FillBackground", 0x00417460, UI_GROUP),
    fill_spec("CScreenGroup_FillBackground_429e50", 0x00429e50, UI_GROUP),
    group_spec("CScreenGroup_OnEnable", 0x004177f0, [anything], exempt_blocks=EMPTY_4177F0),
    group_spec("CControl_OnEnable", 0x00418520, [anything], this=CONTROL),
    group_spec("CGroupControl_OnEnable", 0x0042a000, [anything]),
]

# -- batch 25: std::string compare (msvc6/string.cpp) -----------------------


def compare_world(rng, pool, args):
    """`this` = a string at world+0 over a buffer of small letters (or
    empty with a null pointer); the other text at scratch 1."""
    length = rng.randrange(24)
    null = length == 0 and rng.random() < 0.5
    fields = [(4, 0 if null else WORLD_BASE + SCRATCH, 4), (8, length, 4)]
    letters = [rng.choice(b"ab\x80\xff") for _ in range(32)]
    other = list(letters) if rng.random() < 0.5 else [rng.choice(b"ab\x80\xff") for _ in range(32)]
    for i in range(32):
        fields.append((SCRATCH + i, letters[i], 1))
        fields.append((SCRATCH + SCRATCH_SLOT_BYTES + i, other[i], 1))
    return fields


SPECS += [
    Spec("String_CompareChars", 0x0040a140, "thiscall",
         [below(26), one_of(0, 1, 3, 30, 0xFFFFFFFF), scratch(1), below(26)], presets=compare_world),
]

# -- batch 25: computer-player helpers (ai_helpers.cpp) ---------------------


def count_buildings_world(rng, pool, args):
    count = rng.randrange(30)
    fields = [(BUILDING_COUNT, count, 4)]
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x = rng.randrange(400) if rng.random() < 0.9 else 500
        y = rng.randrange(400)
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1), (record + 6, rng.randrange(3), 1),
                   (record + 0x21, int(rng.random() < 0.25), 1), (record + 0xa, x, 2), (record + 0xc, y, 2)]
        if x < 400:
            fields.append((TILES + (x * 0x300 + y) * 24 + 0x14, rng.randrange(4), 2))
    return fields


def ai_world_400(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, 400, 2), (AI_MAP_SIZES + 2, 400, 2)]


def area_row_world(rng, pool, args):
    x, y = args[0] & 0xFFFF, args[1] & 0xFFFF
    fields = []
    for k in range(20):
        if x + k < 0x300 and y < 0x300:
            value = 0 if rng.random() < 0.8 else rng.randrange(1, 0x10000)
            fields.append((TILES + ((x + k) * 0x300 + y) * 24 + 0x14, value, 2))
    return fields


def ai_small_map(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randrange(1, 60), 2), (AI_MAP_SIZES + 2, rng.randrange(1, 60), 2)]


def list32_world(rng, pool, args):
    return [(0x200 + 2 * i, 0xFFFF if rng.random() < 0.1 else rng.randrange(6), 2) for i in range(50)]


GRID_CELLS = SCRATCH + 2 * SCRATCH_SLOT_BYTES


def grid_world(rng, pool, args):
    return [(0x140, rng.randint(1, 6), 4), (0x144, rng.randint(1, 6), 4), (0x158, WORLD_BASE + GRID_CELLS, 4)]


def small_with_noise(low: int, high: int) -> Generator:
    def generate(rng, pool):
        value = rng.randint(low, high) & 0xFFFF
        return value | (rng.getrandbits(16) << 16) if rng.random() < 0.3 else value
    return generate


def ai_vectors(rng, pool, args):
    """The 20 vectors (begin at -4, end at +0 of each 16-byte entry)."""
    return [(0x007e7a24 + 4 * i, rng.getrandbits(32), 4) for i in range(0x50)]


SPECS += [
    Spec("AI_CountBuildings", 0x0059dac0, "fastcall", [below(3), below(3), one_of(0, 0, 1, 2, 3, 0x10000)],
         presets=count_buildings_world, globals=ai_world_400, max_cases=600),
    Spec("AI_AreaAlongRow", 0x005996c0, "fastcall", [small_with_noise(0, 64), small_with_noise(0, 64)],
         eax_mask=0xFFFF, presets=area_row_world, globals=ai_small_map),
    Spec("AI_List32Add", 0x00551910, "thiscall", [id_with_noise(8)], presets=list32_world),
    Spec("AI_GridCellWrapped", 0x00554730, "thiscall", [anything, anything]),
    Spec("AI_GridSumAround", 0x00554530, "thiscall", [small_with_noise(-1, 6), small_with_noise(-1, 6)],
         presets=grid_world),
    # DEAD 0x55b802: the element copy of an inlined erase(begin, end) whose
    # source range is empty (it starts with source = end).
    Spec("AI_ResetAgentData", 0x0055b770, "thiscall", [], globals=ai_vectors, max_cases=200,
         exempt_blocks=(0x55b802,)),
    Spec("AI_ClearGlobals7e0568", 0x00553e00, "stdcall", [],
         globals=lambda rng, pool, args: [(0x007e0568, rng.getrandbits(32), 4), (0x007e056c, rng.getrandbits(32), 4)]),
    Spec("AI_ListFirst", 0x00559260, "thiscall", [scratch(0)],
         presets=lambda rng, pool, args: [(4, WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES, 4)]),
]

# -- batch 25: graphics and misc helpers ------------------------------------


def frame_offset_world(rng, pool, args):
    group = args[0] >> 16
    return [(0x37c + group * 4, WORLD_BASE + SCRATCH, 4), (SCRATCH + 8, rng.randrange(8), 2),
            (0xc40, rng.randrange(4), 4)]


def frame_id(rng, pool) -> int:
    return (rng.randrange(0xb0) << 16) | rng.randrange(8)


def put_pixel_world(rng, pool, args):
    return [(0x90, 2 * rng.randint(32, 64), 4), (0xa4, WORLD_BASE + SCRATCH, 4)]


def draw_origin(rng, pool, args):
    return [(0x00730f3c, rng.randrange(32), 4), (0x00730f40, rng.randrange(32), 4)]


def equal_ids(rng, pool, args):
    value = rng.getrandbits(32)
    other = value if rng.random() < 0.5 else value ^ (1 << rng.randrange(32))
    return [(4, value, 4), (SCRATCH + 4, other, 4)]


SPECS += [
    Spec("CGraphicsData_VariantFrameAddress", 0x005faaa0, "thiscall", [frame_id, one_of(0, 1, 3, 0xFFFFFFFF)],
         presets=frame_offset_world),
    Spec("Render_PutPixel16", 0x004c7ac0, "thiscall", [below(64), below(64), anything],
         presets=put_pixel_world, globals=draw_origin),
    Spec("Render_Entry204", 0x004c7ab0, "thiscall", [below(256)],
         presets=lambda rng, pool, args: [(0x204, WORLD_BASE + SCRATCH, 4)]),
    Spec("GameParams_CopyRectB0", 0x00437400, "thiscall", [scratch(0)]),
    Spec("Watcom_CopyByte", 0x004584f0, "stdcall", [scratch(0), scratch(1)]),
    Spec("Lobby_EqualField4", 0x00606ac0, "thiscall", [scratch(0)], presets=equal_ids),
    # The C runtime's free (0x621b7e) locks the heap and calls HeapFree: probed.
    Spec("Picture_FreeIfBoth", 0x005aa0a0, "stdcall", [one_of(0, WORLD_BASE + SCRATCH), one_of(0, WORLD_BASE + SCRATCH + 0x100)],
         callees={0x00621b7e: (1, "cdecl", "probe")}),
]

# -- batch 25: simulation helpers (logic_helpers.cpp, map_effects.cpp) ------

RADIUS2 = [0x300, -0x300, 1, -1, 0x301, -0x301, 0x600, 0x601, 0x602, 0x302, 2, -0x2ff, -0x600, -0x601,
           -0x602, -0x302, -2, 0x2ff]


def wear_path_world(rng, pool, args):
    x, y = args
    centre = (x * 0x300 + y) * 24 + TILES
    objects = [0x10, 0x12, 0x13, 0x18, 0x1c, 0x10, 0x12, 0x00, 0x21]
    fields = [(centre + 6, rng.choice(objects), 1)]
    for delta in RADIUS2[:6]:
        value = 0x1c if rng.random() < 0.4 else rng.choice(objects[:7] if rng.random() < 0.95 else objects)
        fields.append((centre + delta * 24 + 6, value, 1))
    return fields


def clear_around_world(rng, pool, args):
    x, y = args
    centre = (x * 0x300 + y) * 24 + TILES
    flags = rng.getrandbits(8)
    fields = [(centre + 0xa, flags if rng.random() < 0.25 else (flags | 0x10) & ~1, 1),
              (centre + 6, 0x20 + rng.randrange(16) if rng.random() < 0.05 else rng.randrange(0x20), 1),
              (0xe0788c, 9999 if rng.random() < 0.1 else rng.randrange(10000), 4)]
    for delta in RADIUS2:
        rare = rng.random() < 0.04
        f = rng.getrandbits(8)
        fields.append((centre + delta * 24 + 0xa, f | 1 if rare else f & ~1, 1))
        rare = rng.random() < 0.04
        fields.append((centre + delta * 24 + 6, 0x20 + rng.randrange(16) if rare else rng.randrange(0x20), 1))
    return fields


GFX_OBJECT = 0x1000000
GFX_POINTERS = 0x1100000
GFX_HEADERS = 0x1200000
_ANIMATION_TABLE: list[int] = []


def animation_table() -> list[int]:
    """The dwords of S3.EXE's table at 0x6a8060 (read once from the file)."""
    if not _ANIMATION_TABLE:
        import os
        import struct

        import pefile
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "Settlers3Ultimate", "S3.EXE")
        pe = pefile.PE(path, fast_load=True)
        data = pe.get_data(0x6a8060 - 0x400000, 4 * 0x800)
        _ANIMATION_TABLE.extend(struct.unpack("<2048I", data))
    return _ANIMATION_TABLE


def settler_animation_world(rng, pool, args):
    settler, kind = args
    race = rng.randrange(4)
    fields = [(0x12a5e60 + settler * 0x40 + 2, race, 1)]
    animation = animation_table()[kind * 4 + race]
    group, index = animation >> 16, animation & 0xFFFF
    if group < 0x130:
        fields += [(GFX_OBJECT + 0xb24 + group * 4, WORLD_BASE + GFX_POINTERS, 4),
                   (GFX_OBJECT + 0x264 + group * 4, 0, 4),
                   (GFX_POINTERS + 8 + index * 4, WORLD_BASE + GFX_HEADERS + rng.randrange(0x100), 4)]
    return fields


def remove_from_groups_globals(rng, pool, args):
    fields = []
    for group in range(10):
        count = 0 if rng.random() < 0.25 else rng.randrange(101)
        fields.append((GROUP_COUNTS + group * 2, count, 2))
        for k in range(100):
            fields.append((GROUP_SETTLERS + group * 200 + k * 2, rng.randrange(6), 2))
    return fields


EFFECT_RECORD = SCRATCH
EFFECT_OUT = SCRATCH + SCRATCH_SLOT_BYTES


def effect_record(rng, pool, args):
    start = rng.randint(-5, 1000)
    duration = -rng.randint(1, 50) if rng.random() < 0.1 else rng.randint(1, 200)
    fields = [(EFFECT_RECORD, start & 0xFFFFFFFF, 4), (EFFECT_RECORD + 4, duration & 0xFFFF, 2),
              (EFFECT_RECORD + 0x28, 0 if rng.random() < 0.3 else rng.randint(-100, 300) & 0xFFFF, 2)]
    for offset in (0x8, 0xc, 0x10, 0x14):
        value = rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-500, 20000) & 0xFFFFFFFF
        fields.append((EFFECT_RECORD + offset, value, 4))
    return fields


def effect_tick(rng, pool) -> int:
    return rng.randint(-20, 1300) & 0xFFFFFFFF


SPECS += [
    Spec("CGameData_WearPath", 0x0051b7f0, "thiscall", [TILE, TILE], presets=wear_path_world),
    Spec("CGameData_QueueMarkedTileWithRoom", 0x0051d000, "thiscall", [TILE, TILE],
         presets=clear_around_world),
    Spec("CGameData_SetSettlerAnimation", 0x0050e2c0, "thiscall", [below(0x8000), below(40)],
         presets=settler_animation_world,
         globals=lambda rng, pool, args: [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4)]),
    Spec("CLogicThread_RemoveSettlerFromGroups", 0x005428e0, "fastcall",
         [anything, anything, id_with_noise(6), one_of(0, 1, 2, 5, 9, 10, 0x10001)], eax_mask=0,
         globals=remove_from_groups_globals, max_cases=500),
    Spec("MapEffect_PositionAt", 0x00544210, "fastcall",
         [one_of(WORLD_BASE + EFFECT_RECORD), anything, effect_tick,
          one_of(WORLD_BASE + EFFECT_OUT, WORLD_BASE + EFFECT_RECORD + 0x8),
          one_of(WORLD_BASE + EFFECT_OUT + 4, WORLD_BASE + EFFECT_RECORD + 0x28)],
         eax_mask=0, presets=effect_record),
    Spec("MapEffects_SetMapSize", 0x00544320, "thiscall",
         [between(-10, 1100), one_of(0xc8, 0, 1, 0xFFFFFFFF, 0x7FFFFFFF)]),
]

# Deliberately wrong pairings for `run.py --self-test`: each must be reported
# as a mismatch, which shows that the harness notices wrong return values,
# wrong memory writes and wrong conditions.
SELF_TEST = [
    Spec("NeighbourX", 0x00503730, "stdcall", [anything, below(8)]),
    Spec("CGameData_CountBuildingsWithFlag21Clear", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_SetBuildingFlag04", 0x004df690, "thiscall", [between(1, 2000)], eax_mask=0,
         presets=building_request_state),
    Spec("CGameData_IsOnMap", 0x00519510, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("CGameData_ClearBuildingFlag08", 0x004df490, "thiscall", [between(1, 2000)],
         eax_mask=0, presets=building_request_state),
    Spec("CGameData_SetAreaBuildingSlotA", 0x004de040, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    Spec("CGameData_ScreenCenterTileX", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("CGameData_FindFreeId4096", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("CGameData_ClearDiagonalTileByteB", 0x00518d10, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=100),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CGameData_FloodFillTileByte10", 0x0051c6c0, "thiscall",
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
    # COM probes: Blt (slot 0x14) against BltFast (slot 0x1c).
    surface_spec("DirectX_CSurface_DDBlt", 0x005bf130,
                 [anything, one_of(SOURCE_SURFACE), one_of(SOURCE_SURFACE), anything, anything]),
    # Callee probes and _CxxThrowException: the DirectPlay check throws for
    # success codes too, with another constructor and throw info.
    Spec("DirectX_ThrowIfDDFailed", 0x005bb890, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS),
]


# -- batch 26: framework helpers (framework_helpers.cpp) ---------------------
#
# Objects at world offset 0 (ECX = the world). Freed pointers point into
# scratch (operator delete is recorded by the fake heap). CloseHandle is an
# API probe (emu.API_HANDLERS).

NET_WIDE = SCRATCH + 12 * SCRATCH_SLOT_BYTES
NET_VECTOR = SCRATCH + 13 * SCRATCH_SLOT_BYTES


def pointer_or_null(offset: int, target: int):
    return lambda rng, pool, args: [(offset, rng.choice([0, WORLD_BASE + target]), 4)]


def handle_world(rng, pool, args):
    return [(4, rng.choice([0, 0, rng.getrandbits(32)]), 4), (8, rng.choice([1, 1, 0, rng.getrandbits(32)]), 4)]


def net_destroy_world(rng, pool, args):
    fields = member_strings((0x18, 0xbc))(rng, pool, args)
    fields.append((0x40, rng.choice([0, WORLD_BASE + NET_VECTOR]), 4))
    wide = rng.choice([0, WORLD_BASE + NET_WIDE + 2])
    fields.append((0x2c, wide, 4))
    fields.append((NET_WIDE + 1, rng.choice([0, 0, 1, 2, 0xfe, 0xff]), 1))
    return fields


SPECS += [
    Spec("Net_DeletePointee_49d980", 0x0049d980, "thiscall", [], eax_mask=0,
         presets=pointer_or_null(0, SCRATCH)),
    Spec("NetSession_SetPair_5bd200", 0x005bd200, "thiscall", [anything, anything]),
    Spec("Lobby_ZeroThreeDwords_5dc810", 0x005dc810, "thiscall", []),
    Spec("Lobby_CPlayerIndex_Construct", 0x006065f0, "thiscall", [anything]),
    Spec("Lobby_CMemoryBlock_Construct", 0x00600e00, "thiscall", []),
    Spec("Lobby_ZeroSixDwords_5ec1e0", 0x005ec1e0, "thiscall", []),
    Spec("Credits_TextStyle_5a0960", 0x005a0960, "thiscall", []),
    Spec("Win32_CFile_Construct", 0x006195c0, "thiscall", []),
    Spec("NetSession_Init_5bb040", 0x005bb040, "thiscall", []),
    Spec("Lobby_Init_5ebfb0", 0x005ebfb0, "thiscall", []),
    Spec("Lobby_FreeBlock_601250", 0x00601250, "thiscall", [], presets=pointer_or_null(4, SCRATCH)),
    Spec("DirectX_CPlayer_Clear", 0x005c0ef0, "thiscall", [], presets=member_strings((0x0c, 0x1c))),
    Spec("Win32_CWin32Handle_Close", 0x0060bf20, "thiscall", [], presets=handle_world),
    Spec("Win32_CThread_Construct", 0x0060df70, "thiscall", [anything, anything]),
    # DEAD 0x49ecf7 / 0x49edbf: the inlined erase() of ~string removes every
    # character, so its _Grow(0, false) is false and the _Eos after it never
    # runs (as in String_Destroy).
    Spec("NetHelp_Destroy_49ec80", 0x0049ec80, "thiscall", [], eax_mask=0, presets=net_destroy_world,
         exempt_blocks=(0x0049ecf7, 0x0049edbf)),
]

# -- batch 26: computer player helpers (ai_small_helpers.cpp, ui_state.cpp) --


def word8_record(rng, pool, args):
    return list32_world(rng, pool, args) + [(SCRATCH + 8, rng.getrandbits(16), 2)]


def tile_words(rng, pool, args):
    return [(SCRATCH, rng.randrange(70), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(70), 2)]


def building_count(rng, pool, args):
    count = rng.randrange(40)
    fields = [(0x1499e60, count, 4)]
    id_ = args[0] & 0xFFFF
    if id_ < 64 and rng.random() < 0.4:
        fields.append((0x1499e64 + id_ * 0x3a, 0xFF, 1))
    return fields


def ui_refresh(rng, pool, args):
    return [(LOGIC_THREAD + 0x84, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4)]


SPECS += [
    Spec("AI_List32AddValue_551780", 0x00551780, "thiscall", [anything], presets=list32_world),
    Spec("AI_List32AddWord8_551760", 0x00551760, "thiscall", [scratch(0)], presets=word8_record),
    Spec("AI_EvaLand_ResetGrid_554070", 0x00554070, "thiscall", []),
    Spec("AI_TileCentre_554880", 0x00554880, "stdcall", [anything, anything, scratch(0), scratch(1)]),
    # Callers use AX; off the map the upper half is the caller's EAX.
    Spec("AI_TileByte8_599570", 0x00599570, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=tile_words, globals=ai_small_map),
    Spec("AI_BlockCentreByte8_554850", 0x00554850, "stdcall",
         [small_with_noise(0, 9), small_with_noise(0, 9)], globals=ai_small_map),
    Spec("AI_EvaLand_GridCellOfTile_554770", 0x00554770, "thiscall", [anything, anything], presets=grid_world),
    Spec("AI_BuildingIfUsed_58e1c0", 0x0058e1c0, "fastcall", [small_with_noise(0, 64)],
         presets=building_count, globals=ai_world),
    Spec("CLogicThread_ClearPendingUiRefresh", 0x0053f530, "logicthread", [], presets=ui_refresh),
]


# -- computer players, batch 1 (session 47): CComputerPlayer, BLACK_BOARD_AGENT,
# the census (ai_computer_player.cpp, ai_census.cpp) -------------------------

AI_PLAYER = 0x007E3130  # g_aiPlayer
AGENT_POOL = SCRATCH  # ten agents of 0x20 bytes (ids 0..9)
AGENT_POOL_SIZE = 10
AI_DATA_NODE = SCRATCH + SCRATCH_SLOT_BYTES  # an AiPlayerData node
AI_SECOND_PLAYER = SCRATCH + 2 * SCRATCH_SLOT_BYTES  # g_aiPlayer for FindChildOfType


def agent_pool(rng, table):
    """Agents 0..9 in the pool, their pointers in the agent table at
    `table` (world offset); children chains only go to higher ids."""
    fields = []
    for agent_id in range(AGENT_POOL_SIZE):
        agent = AGENT_POOL + agent_id * 0x20
        fields.append((table + 4 * agent_id, WORLD_BASE + agent, 4))
        later = AGENT_POOL_SIZE - agent_id - 1
        sibling = 0xFFFF if later == 0 or rng.random() < 0.25 else agent_id + 1 + rng.randrange(later)
        fields += [(agent + 4, rng.randrange(8) if rng.random() < 0.6 else rng.getrandbits(16), 2),
                   (agent + 6, rng.randrange(4) if rng.random() < 0.8 else rng.getrandbits(16), 2),
                   (agent + 8, agent_id if rng.random() < 0.5 else rng.randrange(AGENT_POOL_SIZE), 2),
                   (agent + 0x12, sibling, 2),
                   (agent + 0x14, 0xFFFF if rng.random() < 0.25 else rng.randrange(AGENT_POOL_SIZE), 2)]
    return fields


def computer_player(rng, pool, args):
    """A CComputerPlayer at the world base: its four id lists (small ids,
    a free tail, sometimes full), agents 0..9, an AiPlayerData node."""
    fields = []
    for offset, slots in ((0x4, 100), (0xCE, 50), (0x134, 100), (0x200, 50)):
        used = slots if rng.random() < 0.15 else rng.randrange(slots)
        fields += [(offset + 2 * k, rng.randrange(12) if k < used else 0xFFFF, 2) for k in range(slots)]
    fields += agent_pool(rng, 0x280)
    fields.append((0x778, WORLD_BASE + AI_DATA_NODE, 4))
    for building_type in range(57):
        r = rng.random()
        priority = 0 if r < 0.3 else rng.randrange(1000) if r < 0.5 else rng.getrandbits(16)
        fields.append((AI_DATA_NODE + 0x26 + 6 * building_type, priority, 2))
    return fields


def pool_agent(rng, pool):
    return WORLD_BASE + AGENT_POOL + rng.randrange(AGENT_POOL_SIZE) * 0x20


def agent_with_children(rng, pool, args):
    """The agent at the world base and its children in the pool of the
    player g_aiPlayer points at."""
    return agent_pool(rng, AI_SECOND_PLAYER + 0x280) + [
        (0x14, 0xFFFF if rng.random() < 0.2 else rng.randrange(AGENT_POOL_SIZE), 2)]


def ai_second_player(rng, pool, args):
    return [(AI_PLAYER, WORLD_BASE + AI_SECOND_PLAYER, 4)]


def flag_bits(rng, pool):
    return 1 << rng.randrange(4) if rng.random() < 0.6 else rng.getrandbits(32)


AI_BUILDINGS = 0x1499E64
AI_PLAYERS = 0x11943DC


def ai_map_world(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_WORLD, WORLD_BASE, 4)]


def census_buildings(rng, pool, args):
    """Up to 40 buildings: owners mostly < 20, types mostly < 57, mostly
    finished; sometimes no buildings or a count of 0xffffffff."""
    r = rng.random()
    count = 0 if r < 0.05 else 0xFFFFFFFF if r < 0.08 else rng.randrange(40)
    fields = [(0x1499E60, count, 4)]
    for building_id in range(1, 41):
        b = AI_BUILDINGS + building_id * 0x3A
        fields += [(b, rng.randrange(20) if rng.random() < 0.85 else rng.getrandbits(8), 1),
                   (b + 6, rng.randrange(57) if rng.random() < 0.9 else rng.getrandbits(8), 1),
                   (b + 0x21, 0 if rng.random() < 0.75 else rng.getrandbits(8), 1)]
    return fields


def census_players(rng, pool, args):
    """Players taking part or not, in a few teams; small census counts."""
    fields = []
    for p in range(20):
        record = AI_PLAYERS + p * 0x148
        fields += [(record, 0 if rng.random() < 0.3 else rng.randrange(1, 3), 4),
                   (record + 0xC, rng.randrange(4), 4)]
        fields += [(0x444 + (p * 9 + k) * 4, rng.randrange(50), 4) for k in range(9)]
        fields += [(0x714 + (p * 57 + t) * 4, rng.randrange(10), 4) for t in (15, 16, 18)]
    return fields


def start_position(rng, pool, args):
    """A small map; the player's start position (sometimes off the map)
    with a run of blocked tiles to its right, often to the edge."""
    size = rng.randint(1, 40)
    record = AI_PLAYERS + args[0] * 0x148
    def coordinate():
        return rng.randint(-3, size + 2) & 0xFFFFFFFF if rng.random() < 0.15 else rng.randrange(size)
    x, y = coordinate(), coordinate()
    fields = [(MAP_SIZE, size, 4), (record + 0x10, x, 4), (record + 0x14, y, 4)]
    if x < size and y < size:
        run = rng.randint(0, size - x + 1)
        for k in range(run + 1):
            if x + k >= size:
                break
            t = TILES + ((x + k) * 0x300 + y) * 24
            flags = rng.getrandbits(8)
            fields += [(t + 0xA, flags | 1 if k < run else flags & ~1, 1), (t + 0x10, rng.getrandbits(8), 1)]
    return fields


def ptr_vector(rng, pool, args):
    first = 0 if rng.random() < 0.2 else WORLD_BASE + SCRATCH + 4 * rng.randrange(16)
    last = first + 4 * rng.randrange(20) if rng.random() < 0.8 else rng.getrandbits(32)
    return [(4, first, 4), (8, last & 0xFFFFFFFF, 4)]


class CopyRange:
    """Arguments of AI_UninitializedCopyPtrs: (first, last, dest). A null
    destination only comes with a range of at most one element (the
    original then writes to address 4 onwards)."""

    def __init__(self):
        self.count = 0

    def first(self, rng, pool):
        return WORLD_BASE + SCRATCH

    def last(self, rng, pool):
        self.count = rng.randrange(20)
        return WORLD_BASE + SCRATCH + 4 * self.count

    def dest(self, rng, pool):
        if self.count <= 1 and rng.random() < 0.5:
            return 0
        return WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 4 * rng.randrange(4)


COPY_RANGE = CopyRange()


def grid_world_or_empty(rng, pool, args):
    return [(0x140, rng.randint(0, 6), 4), (0x144, rng.randint(0, 6), 4), (0x158, WORLD_BASE + GRID_CELLS, 4)]


SPECS += [
    Spec("CComputerPlayer_AddRootNote", 0x005517f0, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemoveRootNote", 0x00551820, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_AddPendingNote", 0x00551850, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemovePendingNote", 0x00551880, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_AddActiveNote", 0x005518b0, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemoveActiveNote", 0x005518e0, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_RemoveFromList200", 0x00551940, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_ActivateNote", 0x005515e0, "thiscall", [id_with_noise(AGENT_POOL_SIZE)],
         presets=computer_player),
    # EDX's upper half goes into the pushed id (mov dx).
    Spec("CComputerPlayer_ActivateNoteOf", 0x005515c0, "thiscall", [pool_agent], presets=computer_player),
    Spec("CComputerPlayer_DeactivateNote", 0x00551630, "thiscall", [id_with_noise(AGENT_POOL_SIZE)],
         presets=computer_player),
    Spec("CComputerPlayer_DeactivateNoteOf", 0x00551610, "thiscall", [pool_agent], presets=computer_player),
    Spec("CComputerPlayer_RaiseBuildPriorities", 0x00551b10, "thiscall", [], eax_mask=0,
         presets=computer_player),
    Spec("BLACK_BOARD_NOTE_ResetLinks", 0x00582400, "thiscall", []),
    Spec("BLACK_BOARD_NOTE_Init", 0x005823e0, "thiscall", [anything]),
    # AX only: the upper half of EAX is the caller's.
    Spec("BLACK_BOARD_NOTE_MarkDispatched", 0x00582430, "thiscall", [], eax_mask=0xFFFF),
    Spec("BLACK_BOARD_NOTE_SetFlags", 0x00582440, "thiscall", [flag_bits], eax_mask=0xFFFF),
    Spec("BLACK_BOARD_NOTE_ClearFlags", 0x00582450, "thiscall", [flag_bits]),
    Spec("BLACK_BOARD_NOTE_HasFlags", 0x00582460, "thiscall", [flag_bits]),
    Spec("BLACK_BOARD_NOTE_FindChildOfType", 0x005824d0, "thiscall", [id_with_noise(5)],
         presets=agent_with_children, globals=ai_second_player),
    Spec("AI_Census_Construct", 0x0055b6b0, "thiscall", [], globals=ai_vectors, max_cases=200),
    Spec("AI_Census_CountBuildings", 0x0055be00, "thiscall", [], presets=census_buildings,
         globals=ai_map_world, max_cases=400),
    Spec("AI_Census_ComputeTeamStrength", 0x0055bea0, "thiscall", [], presets=census_players,
         globals=ai_map_world, max_cases=400),
    Spec("AI_StartPositionWalkRegion", 0x0055b6e0, "stdcall", [below(20)], presets=start_position,
         globals=ai_map_world),
    Spec("AI_PtrVector_Size", 0x0055bfc0, "thiscall", [], presets=ptr_vector),
    Spec("AI_UninitializedCopyPtrs", 0x0055bff0, "stdcall", [COPY_RANGE.first, COPY_RANGE.last, COPY_RANGE.dest]),
    Spec("AI_EvaLand_SumAllCells", 0x005546c0, "thiscall", [], presets=grid_world_or_empty, max_cases=500),
    # The agent constructors: EAX is not used (the factory 0x56f6d0 returns the object).
    Spec("GLOBAL_EXPAND_EMPIRE_AGENT_Construct", 0x0056cd00, "thiscall", [], eax_mask=0),
    Spec("COMPUTER_PLAYER_AGENT_Construct", 0x00584680, "thiscall", [], eax_mask=0),
    Spec("T_MAP_WORKER_AGENT_Construct", 0x0057cda0, "thiscall", [], eax_mask=0),
    Spec("T_ATTACK_AGENT_Construct", 0x0055ebe0, "thiscall", [], eax_mask=0),
    Spec("T_ECONOMY_AGENT_Construct", 0x00567850, "thiscall", [], eax_mask=0),
    Spec("T_SAFETY_AGENT_Construct", 0x00586830, "thiscall", [], eax_mask=0),
    Spec("CCentrumCheckAgent_Construct", 0x00560dd0, "thiscall", [], eax_mask=0),
    Spec("AI_List_Begin", 0x00580220, "thiscall", [scratch(0)]),
    Spec("AI_List_Empty", 0x00580230, "thiscall", [],
         presets=lambda rng, pool, args: [(0xC, 0, 4)] if rng.random() < 0.4 else []),
    Spec("AI_Pair5_Construct", 0x0059c5a0, "thiscall", [scratch(0), scratch(1)]),
    Spec("AI_CountPlayersTakingPart", 0x00572370, "stdcall", [], globals=ai_world,
         presets=lambda rng, pool, args: [(AI_PLAYERS + p * 0x148, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4)
                                          for p in range(20)]),
]

# -- batch 26: exception classes (win32_exceptions.cpp) -----------------------
#
# The constructors build the object at world offset 0 (its message string
# at +8) from the string in scratch slot 0 (string_world's second string;
# sometimes the object's own message member, the self-assignment path).
# ThrowDataError gets two strings from member_strings (STRING_OTHER and
# STRING_OTHER + 0x10) and throws through _CxxThrowException.


def message_ptr(rng, pool) -> int:
    return WORLD_BASE + 8 if rng.random() < 0.1 else STRING_OTHER_PTR


def mostly_derived(rng, pool) -> int:
    return rng.choice([0, 1, rng.getrandbits(32)])


SPECS += [
    # DEAD: 0x611a7d caps a length above 0xFFFFFFFF (`cmp ebx, -1; jbe`);
    # 0x611a8e, 0x611aa9 and 0x611ad1 are the _Xran, erase-memmove and _Eos
    # of the inlined self-assignment, whose length the constructor has just
    # set to 0 (the message member is cleared before assign() reads it).
    Spec("Win32_CException_Construct", 0x00611a20, "thiscall", [message_ptr, mostly_derived], presets=string_world,
         exempt_blocks=(0x00611a7d, 0x00611a8e, 0x00611aa9, 0x00611ad1)),
    Spec("Win32_CRunTimeError_Construct", 0x00614c60, "thiscall", [message_ptr, mostly_derived], presets=string_world),
    Spec("Lobby_CDataError_Construct", 0x006003b0, "thiscall", [message_ptr, mostly_derived], presets=string_world),
    Spec("Lobby_ThrowDataError", 0x005ffcc0, "fastcall",
         [one_of(STRING_OTHER_PTR), one_of(STRING_OTHER_PTR + 0x10), anything],
         presets=member_strings((), (0x0, 0x10)), new_fails=0.2, new_fails_size=0x18),
]

# -- batch 26: CSurface release and destructors (directx_surface.cpp) --------
#
# The surface at world offset 0 with its interface (+4) at SURFACE_IFACE
# (vtable = emu.FAKE_VTABLE, slot 8 = IUnknown::Release, a COM probe).


def surface_release_world(rng, pool, args):
    blocks = [(offset, rng.choice([0, WORLD_BASE + SCRATCH + offset]), 4) for offset in (0x94, 0x98, 0x9c, 0xa0, 0xa4)]
    return [(4, rng.choice([SURFACE_IFACE, SURFACE_IFACE, 0]), 4),
            (SURFACE_IFACE - WORLD_BASE, FAKE_VTABLE, 4)] + blocks


def surface_release_always(rng, pool, args):
    return [(4, SURFACE_IFACE, 4), (SURFACE_IFACE - WORLD_BASE, FAKE_VTABLE, 4)]


RELEASE_VIRTUALS = {0x08: (1, {}, {"com": True})}

SPECS += [
    Spec("DirectX_CSurface_Release", 0x005bf2c0, "thiscall", [], presets=surface_release_always,
         virtuals=RELEASE_VIRTUALS),
    Spec("DirectX_CSurface_Destroy", 0x005bf240, "thiscall", [], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("DirectX_CSurface_DeletingDestroy", 0x005bf220, "thiscall", [anything], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("CLandscapeSurface_Destroy", 0x0054be70, "thiscall", [], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("CLandscapeSurface_DeletingDestroy", 0x0054be40, "thiscall", [anything], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
]

# -- batch 26: adding map effects (map_effects.cpp) --------------------------
#
# The effect table at world offset 0: slot count +0x668a0 (small, sometimes
# at the 4999 limit), blocks per row +0x668a4, records 1..count+12 with a
# random "in use" dword +0 and a valid next link +0x48, valid list heads
# for the first 1400 cells (sometimes -1). Coordinates stay small so every
# cell is a real head. The spawn functions' records get their padding bytes
# from the make functions' stack temporaries: ignored for the slots they
# can use (1..64; the count stays below 50 for them).

EFFECT_COUNT = 0x668a0
EFFECT_HEADS = 0x5cc60


def effect_table(full_chance: float):
    def preset(rng, pool, args):
        full = rng.random() < full_chance
        used = 4985 + rng.randrange(15) if full else rng.randrange(45)
        last = min(used + 12, 4999)
        fields = [(EFFECT_COUNT, used, 4), (0x668a4, rng.randint(1, 40), 4)]
        free_slot = rng.randint(1, used) if full and used and rng.random() < 0.5 else None
        for slot in range(1, last + 1):
            if full and slot != free_slot:
                in_use = 1 + rng.randrange(0xFFFF)  # (not 0: no free record before the end)
            else:
                in_use = 0 if (not full and rng.random() < 0.15) or slot == free_slot else 1 + rng.randrange(0xFFFF)
            fields.append((slot * 0x4c, in_use, 4))
            if not full or slot > used - 20:
                fields.append((slot * 0x4c + 0x48, 0 if rng.random() < 0.3 else rng.randrange(last + 1), 4))
        for cell in range(1400):
            fields.append((EFFECT_HEADS + cell * 4, 0xFFFFFFFF if rng.random() < 0.05 else rng.randrange(last + 1), 4))
        return fields
    return preset


def effect_source(rng, pool, args):
    fields = effect_table(0.1)(rng, pool, args)
    used = fields[0][1]
    fields += [(SCRATCH + 0x10, rng.randrange(7000), 4), (SCRATCH + 0x14, rng.randrange(7000), 4),
               (SCRATCH + 0x48, rng.randrange(min(used + 12, 4999) + 1), 4)]
    return fields


def coordinate(rng, pool) -> int:
    return rng.randrange(7000)


EFFECT_TABLE_PADDING = tuple(slot * 0x4c + p for slot in range(1, 65) for p in EFFECT_PADDING)


def spawn_spec(name, address, args, **kwargs):
    return Spec(name, address, "thiscall", args, presets=effect_table(0.0), ignore_bytes=EFFECT_TABLE_PADDING,
                max_cases=150, **kwargs)


SPECS += [
    Spec("MapEffects_FindFreeSlot", 0x00545ee0, "thiscall", [], presets=effect_table(0.3), max_cases=150),
    Spec("MapEffects_CellOfPosition", 0x00545eb0, "thiscall", [anything, anything],
         presets=lambda rng, pool, args: [(0x668a4, rng.choice([1, 38, rng.getrandbits(32)]), 4)]),
    Spec("MapEffects_LinkIntoCell", 0x00545df0, "thiscall", [below(1400), between(1, 56)],
         presets=effect_table(0.0), max_cases=150),
    Spec("MapEffects_Add", 0x005445e0, "thiscall", [scratch(0)], presets=effect_source, max_cases=200),
    spawn_spec("MapEffects_Spawn_5450d0", 0x005450d0, [anything] + [coordinate] * 4),
    spawn_spec("MapEffects_Spawn_545120", 0x00545120, [anything] + [coordinate] * 4),
    spawn_spec("MapEffects_Spawn_544f20", 0x00544f20, [anything] + [coordinate] * 4),
    spawn_spec("MapEffects_Spawn_545070", 0x00545070, [anything] + [coordinate] * 4),
    spawn_spec("MapEffects_Spawn_544f80", 0x00544f80, [anything] + [coordinate] * 4),
    spawn_spec("MapEffects_Spawn_544dc0", 0x00544dc0, [coordinate, below(4)] + [coordinate] * 5),
    spawn_spec("MapEffects_Spawn_544e70", 0x00544e70,
               [anything] + [coordinate] * 5 + [lambda rng, pool: rng.choice([3, 0x103, 2, rng.getrandbits(32)])]),
    spawn_spec("MapEffects_Spawn_545860", 0x00545860, [anything] + [coordinate] * 8),
]

SELF_TEST += [
    # API probe CloseHandle against operator delete of +4 (and +0xc cleared).
    Spec("Lobby_FreeBlock_601250", 0x0060bf20, "thiscall", [], presets=handle_world),
    # ignore_bytes on the effect table: the two spawn functions differ only
    # in the stationary record's kind (dword +0x34: 0x63 against 0x5d), a few
    # bytes from the ignored padding bytes +0x3b / +0x3e.
    spawn_spec("MapEffects_Spawn_545070", 0x00544f20, [anything] + [coordinate] * 4),
]


# -- batch 27: assertion and out-of-memory errors (win32_errors.cpp) and the
# default-allocator substring constructor (msvc6/string.cpp) ---------------
#
# Objects at world offset 0 as in batch 26 (message string at +8, from
# scratch slot 0). CAssertmentFailed's expression and file are the C text
# of string_world or null; 0x7a7868 (the "" the message is built from) is
# sometimes non-empty (empty_text).


def assertment_object(rng, pool, args):
    """string_world, plus the expression, file and line of a CAssertmentFailed
    at world offset 0 (for its text function)."""
    fields = string_world(rng, pool, args)
    return fields + [(0x18, text_or_null(rng, pool), 4), (0x1c, text_or_null(rng, pool), 4),
                     (0x20, rng.choice([0, 1, 99, 1234, rng.getrandbits(32)]), 4)]


SPECS += [
    # Dead, as at String_FromSubstr: the self-assignment path on a
    # just-emptied string (_Xran, the memmove, _Eos, the second _Xran
    # check, the copy and the release of its null old buffer).
    # globals: the empty string's count byte is set per case; a value of
    # 0xfe or more left behind by an earlier spec in a full run otherwise
    # hides the sharing path (0x408dde).
    Spec("String_FromSubstrDefault", 0x00408cc0, "thiscall", [SELF_OR_OTHER, string_count, string_count],
         presets=string_world, globals=null_string_count,
         exempt_blocks=(0x00408d14, 0x00408d2f, 0x00408d57, 0x00408d6f, 0x00408d79, 0x00408da3)),
    Spec("Win32_CProgrammingError_Construct", 0x00614eb0, "thiscall", [message_ptr, mostly_derived],
         presets=string_world),
    # Dead: 0x612700 is the _Eos after erase(0, npos) of a temporary; the
    # new length is always 0, for which _Grow returns false.
    Spec("Win32_COutOfMemory_Construct", 0x006124a0, "thiscall", [message_ptr, mostly_derived],
         presets=string_world, exempt_blocks=(0x00612700,)),
    # ignore_bytes: the result's allocator byte is the low byte of the
    # address of the original's last stack temporary (0x408cc0 takes it from
    # its argument's slot), ours that of our own temporary; MSVC 6.0's
    # allocator is an empty class that is never read.
    Spec("Win32_CAssertmentFailed_Describe", 0x00612e80, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=assertment_object, ignore_bytes=(STRING_RESULT,)),
    # Dead: 0x612b72 and 0x612c2e are the _Eos after erase(0, npos) (as
    # above); 0x612bcd / 0x612bd1 split the text from 0x612e80 (called
    # directly) when its buffer is shared, but that function frees the
    # temporary its result shared with, so the count is always 0.
    Spec("Win32_CAssertmentFailed_Construct", 0x00612a20, "thiscall",
         [text_or_null, text_or_null, anything, mostly_derived], presets=string_world, globals=empty_text,
         exempt_blocks=(0x00612b72, 0x00612bcd, 0x00612bd1, 0x00612c2e)),
    Spec("Win32_ThrowAssertmentFailed", 0x006119b0, "fastcall", [text_or_null, text_or_null, anything],
         presets=string_world, globals=empty_text, new_fails=0.2, new_fails_size=0x24),
    Spec("Win32_ThrowOutOfMemory", 0x00610960, "fastcall", [text_or_null, text_or_null, anything],
         presets=string_world, new_fails=0.2, new_fails_size=0x18),
]

SELF_TEST += [
    # ignore_bytes on the result's allocator byte: the wrong text function
    # drops the newline, so the length dword (+8) and the text differ.
    Spec("WrongDescribe", 0x00612e80, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=assertment_object, ignore_bytes=(STRING_RESULT,)),
]


# -- batch 27: Lobby::CMemoryBlock and CLMGeneric (lobby_memory_block.cpp) ---
#
# `this` is a block at world offset 0 (block functions) or a message at 0
# whose block is at +4 (CLMGeneric). The other block / message is in scratch
# slot 0; data blocks are in slots 1 and 2 (lazily random bytes).

BLOCK_OTHER = SCRATCH
BLOCK_DATA = SCRATCH + 1 * SCRATCH_SLOT_BYTES
BLOCK_OTHER_DATA = SCRATCH + 2 * SCRATCH_SLOT_BYTES
BLOCK_SOURCE = SCRATCH + 3 * SCRATCH_SLOT_BYTES


def block_fields(rng, offset: int, data: int):
    used = rng.choice([0, 0, 1, 4, 15, 16, 17, rng.randrange(0x40)])
    has_data = rng.random() < 0.85 or used == 0 and rng.random() < 0.5
    reserved = rng.choice([used, used, (used + 15) & ~15, rng.randrange(0x50)])
    return [(offset + 4, WORLD_BASE + data if has_data else 0, 4), (offset + 8, reserved, 4),
            (offset + 0xc, used if has_data else rng.choice([0, used]), 4)]


def block_world(at: int):
    """A block at world offset `at` (0, or 4 inside a message) and another at
    the same offset in scratch slot 0."""
    def presets(rng, pool, args):
        return block_fields(rng, at, BLOCK_DATA) + block_fields(rng, BLOCK_OTHER + at, BLOCK_OTHER_DATA)
    return presets


def block_size(rng, pool) -> int:
    return rng.choice([0, 1, 4, 15, 16, 17, 32, 48, rng.randrange(0x60)])


SPECS += [
    Spec("Lobby_MemoryBlock_Size", 0x00601270, "thiscall", [], presets=block_world(0)),
    # 0x60134b: the catch(...) handler (only reachable by an exception).
    Spec("Lobby_MemoryBlock_Reserve", 0x00601280, "thiscall", [block_size], eax_mask=0, presets=block_world(0),
         new_fails=0.15, exempt_blocks=(0x0060134b,)),
    Spec("Lobby_MemoryBlock_Append", 0x00601170, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)], eax_mask=0,
         presets=block_world(0), new_fails=0.1),
    # 0x60122d: the catch(...) handler.
    Spec("Lobby_MemoryBlock_AppendBytes", 0x006011c0, "thiscall", [one_of(WORLD_BASE + BLOCK_SOURCE), block_size],
         eax_mask=0, presets=block_world(0), new_fails=0.1, exempt_blocks=(0x0060122d,)),
    Spec("Lobby_MemoryBlock_At", 0x00600ee0, "thiscall", [below(0x42)], presets=block_world(0), new_fails=0.1),
    Spec("Lobby_MemoryBlock_Equal", 0x00600ea0, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(0)),
    Spec("Lobby_MemoryBlock_Copy", 0x00600e40, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)],
         presets=block_world(0), new_fails=0.1),
    # cover_until: nop padding follows the tail jump.
    Spec("Lobby_MemoryBlock_Destroy", 0x00600e70, "thiscall", [], presets=block_world(0), cover_until=0x00600e7b),
    Spec("Lobby_CLMGeneric_SetHeader", 0x00603270, "thiscall", [anything], eax_mask=0, presets=block_world(4),
         new_fails=0.1),
    Spec("Lobby_CLMGeneric_Construct", 0x00601e80, "thiscall", [anything], presets=block_world(4), new_fails=0.1),
    Spec("Lobby_CLMGeneric_Equal", 0x006027d0, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(4)),
    Spec("Lobby_CLMGeneric_Destroy", 0x00602190, "thiscall", [], presets=block_world(4), cover_until=0x0060219e),
]


# -- batch 27: CStaticText (ui_static_text.cpp) ------------------------------
#
# The object at world offset 0; its text from string_world's second string
# (scratch slot 0); the rectangle in scratch slot 5 (lazily random). The
# text member's allocator byte is the low byte of the rectangle's address.

SPECS += [
    Spec("CStaticText_Construct", 0x0041c3e0, "thiscall",
         [one_of(WORLD_BASE + SCRATCH + 5 * SCRATCH_SLOT_BYTES, WORLD_BASE + SCRATCH + 5 * SCRATCH_SLOT_BYTES + 0x31),
          one_of(STRING_OTHER_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR, WORLD_BASE + 0x28),
          one_of(0, 0, 0x24, 7, 0x12345678)],
         presets=string_world, globals=null_string_count,  # see String_FromSubstrDefault (0x41c4c6)
         # Dead: 0x41c434 caps a length above 0xFFFFFFFF; 0x41c445, 0x41c460
         # and 0x41c488 are the _Xran, erase-memmove and _Eos of the
         # self-assignment, whose length was just set to 0; 0x41c4f9 copies
         # from the static empty string when the source has no buffer but a
         # non-zero length (an inconsistent string).
         exempt_blocks=(0x0041c434, 0x0041c445, 0x0041c460, 0x0041c488, 0x0041c4f9)),
    Spec("CStaticText_SetStyle", 0x0041ca60, "thiscall", [anything]),
]


# -- batch 27: Watcom vector of strings, resize (watcom/string_vector.cpp) ---
#
# The vector at world offset 0; its entries (16-byte strings) in scratch
# slot 0, their buffers in slots 1.. (one 0x100-byte area per entry, some
# shared). The new strings' allocator byte is the low byte of n.


def string_vector_state(rng, pool, args):
    capacity = rng.choice([0, 1, 2, 3, 4, 5, 6])
    filled = rng.randint(0, capacity) if rng.random() < 0.9 else rng.randint(-2, 0) & 0xFFFFFFFF
    fields = [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH if capacity else rng.choice([0, WORLD_BASE + SCRATCH]), 4),
              (0xc, capacity, 4), (0x10, filled, 4), (0x14, rng.randrange(8), 4)]
    ptrs = []
    for i in range(capacity):
        shared = rng.choice(ptrs) if ptrs and rng.random() < 0.3 else None
        entry, ptr = string_fields(rng, SCRATCH + 16 * i, SCRATCH + SCRATCH_SLOT_BYTES + 0x100 * i,
                                   shared_ptr=shared)
        fields += entry
        if ptr:
            ptrs.append(ptr)
    return fields


SPECS += [
    # Dead, in the inlined destructor of the old entries: 0x40a6d0 is the
    # _Eos after erase(0, npos) (new length 0: _Grow returns false); 0x40a6fc
    # decrements the count byte, but the split before the erase left the
    # buffer unshared (count 0) or frozen.
    Spec("StringVector_Resize", 0x0040a580, "thiscall", [small_or_anything(0, 8)], presets=string_vector_state,
         new_fails=0.1, exempt_blocks=(0x0040a6d0, 0x0040a6fc)),
]


# -- batch 27: the "roomy" region flood fill (flood_fills.cpp) ---------------
#
# Tiles of the small map mostly marked 0x10 and unblocked, terrain rarely of
# class 0x2_ (the room check of 0x51d000 looks two steps around each tile;
# tiles beyond the map are lazily random).


def flood_roomy(rng, pool, args):
    # The big map (all of it roomy) wraps the 10,000-entry ring.
    n, big = flood_map(rng, args, big_chance=0.03)
    blocked = 0.0 if big else rng.choice([0.0, 0.02, 0.1])
    fields = [(MAP_SIZE, n, 4)]
    for x in range(-2, n + 2):
        for y in range(-2, n + 2):
            if x * 0x300 + y < 0:
                continue
            flags = rng.getrandbits(8)
            if rng.random() >= blocked:
                flags = (flags | 0x10) & ~0x01
            terrain = rng.getrandbits(8)
            if terrain & 0xf0 == 0x20 and (big or rng.random() < 0.9):
                terrain ^= 0x40
            offset = tile(x, y)
            fields += [(offset + 0xa, flags, 1), (offset + 0x6, terrain, 1)]
    return fields


SPECS += [
    Spec("CGameData_FloodSetWord16RoomyTiles", 0x0051d2b0, "thiscall",
         [below(12), below(12), anything], eax_mask=0, presets=flood_roomy, max_cases=150,
         # A fill of the whole 101 x 101 map runs the two-step room check
         # 60,000 times; our debug build needs more than 20M instructions.
         max_instructions=100_000_000),
]


# -- batch 27: the shared land evaluation object (ai_evaland.cpp) ------------
#
# The object at world offset 0 (grid pointer +0x158 into scratch or null).
# The CRT's eh vector destructor iterator (0x61f18a) is a callee probe: it
# would run the original list destructor 0x5591e0 on the 20 lists, which
# needs real list nodes (in-process tests cover that); here only its
# arguments are compared.

EH_VECTOR_DESTRUCTOR = {0x0061f18a: (4, "stdcall", "probe")}


def evaland_object(rng, pool, args):
    return [(0x158, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]


def evaland_globals(rng, pool, args):
    return [(0x007e0568, rng.choice([1, 1, 2, 3, 0, rng.getrandbits(32)]), 4),
            (0x007e056c, rng.choice([WORLD_BASE, WORLD_BASE, 0]), 4),
            (0x007e3134, rng.getrandbits(32), 4)]


SPECS += [
    Spec("AI_EvaLand_Destroy", 0x00553fe0, "thiscall", [], eax_mask=0, presets=evaland_object,
         globals=evaland_globals, callees=EH_VECTOR_DESTRUCTOR),
    Spec("AI_EvaLand_Release", 0x00553dc0, "stdcall", [], presets=evaland_object, globals=evaland_globals,
         callees=EH_VECTOR_DESTRUCTOR),
]


# -- batch 27: small framework, container, UI and graphics members
# (framework_small.cpp) and string::compare(const char*) -------------------
#
# Objects at world offset 0; nodes, outputs and data in scratch slots.


def field_or_zero(offset: int, target: int):
    return lambda rng, pool, args: [(offset, rng.choice([0, WORLD_BASE + target, rng.getrandbits(32)]), 4)]


def gfx_tables(table_field: int):
    def presets(rng, pool, args):
        return [(table_field + 4 * g, WORLD_BASE + SCRATCH + (1 + g) * SCRATCH_SLOT_BYTES, 4) for g in range(4)]
    return presets


def gfx_id(rng, pool) -> int:
    return rng.randrange(4) << 16 | rng.choice([0, 1, 2, 5, 9, rng.randrange(40)])


SPECS += [
    Spec("WCIsvListBase_Destroy", 0x0060b5a0, "thiscall", [], presets=field_or_zero(8, SCRATCH)),
    Spec("WCIsvList_Insert", 0x0060b430, "thiscall", [one_of(WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES)],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("WCIntVector_Construct", 0x004376b0, "thiscall", [small_or_anything(0, 6), anything], new_fails=0.2),
    Spec("WCIntVector_Destroy", 0x00414590, "thiscall", [],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("Lobby_ScaleBy4_44c450", 0x0044c450, "stdcall", [scratch(0), anything, anything, anything, anything]),
    Spec("Win32_CWin32Handle_Set", 0x0060bf00, "thiscall", [anything, anything]),
    Spec("Lobby_MemoryBlock_Assign", 0x00600e80, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(0), new_fails=0.1),
    Spec("Win32_CRegistryKey_Construct", 0x00616430, "thiscall", []),
    Spec("Win32_CEvent_Construct", 0x0060c510, "thiscall", []),
    Spec("Lobby_HasField4_5da3a0", 0x005da3a0, "thiscall", [],
         presets=lambda rng, pool, args: [(4, rng.choice([0, rng.getrandbits(32)]), 4)]),
    Spec("UI_CopyWords_418fb0", 0x00418fb0, "fastcall", [scratch(0), scratch(1), below(20)]),
    Spec("UI_WideEos_428880", 0x00428880, "thiscall", [below(30)],
         presets=lambda rng, pool, args: [(4, WORLD_BASE + SCRATCH, 4)]),
    Spec("CGraphicsData_FramePointer", 0x005fac70, "thiscall", [scratch(6), gfx_id], presets=gfx_tables(0x5ac)),
    Spec("CGraphicsData_AnimationPointer", 0x005faaf0, "thiscall", [scratch(6), gfx_id],
         presets=gfx_tables(0x494)),
    Spec("String_CompareText", 0x0040a0e0, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world),
]


def message_block_source(rng, pool, args):
    """block_world(4) (the message at world 0, another in scratch slot 0)
    plus random fields +0x14..+0x20 of the source."""
    return block_world(4)(rng, pool, args)


SPECS += [
    Spec("WCUIntVector_Destroy", 0x00458590, "thiscall", [],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("WCIntVector_ConstructBare", 0x00437730, "thiscall", [small_or_anything(0, 6)], new_fails=0.2),
    Spec("Lobby_CLMMessageBlock_Copy", 0x00601470, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)],
         presets=message_block_source, new_fails=0.1),
]


# -- batch 28: building sites (building_sites.cpp) --------------------------
#
# CheckBuildingSite 0x4d99e0 (with its callees PlanSiteLevelling 0x4da440
# and AddBuildingSite 0x4dd280, which run as original code inside it) on a
# preset 31x31 window of tiles around (x, y): heights a tilted plane with
# noise (sometimes flat, sometimes steep), mostly owned by the player (or
# nobody for modes 2/3), ground per race group, sparse flags/objects, one
# area; the player's race 0..3 (it indexes the layout tables in S3.EXE), the
# offset table up to ring 22, free levelling slots and building records.

SITE_TILES = 0x67444
SITE_OFFSETS = 0x110a588
SITE_CACHE = 0x1196024
SITE_BUILDINGS = 0x1499e64
SITE_TYPES = [7, 8, 9, 0x1d, 0x25, 0x38, 0x20, 0x21, 1, 2, 3, 5, 0xe, 0xf, 0x18, 0x19, 0x1e, 0x24, 0x33]


def site_type(rng, pool) -> int:
    return rng.choice(SITE_TYPES) if rng.random() < 0.5 else rng.randint(1, 56)


def site_tile(x: int, y: int) -> int:
    return SITE_TILES + ((x * 0x300 + y) & 0xFFFFFFFF) * 24


def hex_distance(dx: int, dy: int) -> int:
    if (dx >= 0) == (dy >= 0):
        return max(abs(dx), abs(dy))
    return abs(dx) + abs(dy)


def site_offset_table(rng, rings=22):
    import math
    fields = []
    entry = 0
    for ring in range(rings + 1):
        for dx in range(-ring, ring + 1):
            for dy in range(-ring, ring + 1):
                if hex_distance(dx, dy) != ring:
                    continue
                angle = math.atan2(0.8660254 * dy, dx - 0.5 * dy) * 3 / math.pi
                sector = 0 if ring == 0 else int(angle % 6) % 6
                e = SITE_OFFSETS + entry * 16
                fields += [(e, dx & 0xFFFFFFFF, 4), (e + 4, dy & 0xFFFFFFFF, 4), (e + 8, ring, 4), (e + 12, sector, 4)]
                entry += 1
    if rng.random() < 0.03:
        fields.append((SITE_OFFSETS + 8, 21, 4))  # entry 0 beyond the water range
    return fields


def site_player(rng, player):
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    return [(record + 4, rng.randrange(4), 4),
            (record + 0x84, rng.choice([60001, 60002, 0xffff, rng.randrange(60000, 70000)]), 4)]


def site_window(rng, x, y, owner, flat=False, height=None):
    """The 31x31 tiles around (x, y); `height` fixes the base height (a flat
    window at the site's own height needs no earth)."""
    fields = []
    base = rng.randint(10, 120)
    if height is not None:
        base = height
    slopes = [0, 0, 0, 1, 2, 3, 4, 6, 9]
    sx = 0 if flat else rng.choice(slopes) * rng.choice([1, -1])
    sy = 0 if flat else rng.choice(slopes) * rng.choice([1, -1])
    noise = 0 if flat else rng.choice([0, 1, 2])
    own = rng.choice([1.0, 1.0, 0.99, 0.9])
    flag_rate = rng.choice([0, 0, 0.005, 0.03])
    ground = rng.choice([0x10, 0x10, 0x40, 0x90])
    other_ground = rng.choice([0x30, 0x20, 0x40, 0x90, 0x04])
    other_rate = rng.choice([0, 0, 0.01, 0.05])
    area = rng.randrange(1, 40)
    bump = rng.random() < 0.2
    # A hill or hollow under the building (on otherwise even land): the
    # earth to move spreads over all size classes.
    dip = rng.randint(-40, 40) if rng.random() < 0.4 else 0
    dip_radius = rng.randint(1, 6)
    for i in range(31):
        for j in range(31):
            tx, ty = x - 15 + i, y - 15 + j
            t = site_tile(tx, ty)
            h = base + sx * (i - 15) // 2 + sy * (j - 15) // 2 + (rng.randint(-noise, noise) if noise else 0)
            if bump and abs(i - 12) < 3 and abs(j - 18) < 3:
                h += 30
            if dip and hex_distance(i - 15, j - 15) <= dip_radius:
                h += dip * (dip_radius + 1 - hex_distance(i - 15, j - 15)) // (dip_radius + 1)
            h = min(max(h, 0), 255)
            terrain = ground + (rng.randrange(4) if rng.random() < 0.2 else 0)
            if rng.random() < other_rate:
                terrain = other_ground
            flags = 0
            if rng.random() < flag_rate:
                flags = rng.choice([1, 4, 8, 2, 0x40])
            fields += [(t + 4, h, 1), (t + 6, terrain, 1), (t + 8, owner if rng.random() < own else rng.randrange(256), 1),
                       (t + 0xa, flags, 1), (t + 2, 0 if rng.random() > flag_rate else rng.randrange(1, 100), 2),
                       (t + 0x14, area if rng.random() > other_rate else area + 1, 2)]
    if rng.random() < 0.5:
        fields.append((site_tile(x, y) + 6, 0x20 + rng.randrange(4), 1))  # mountain under the building
    return fields


def site_buildings(rng):
    fields = []
    if rng.random() < 0.02:
        return [(SITE_BUILDINGS + b * 0x3a, 1, 1) for b in range(1, 0xfa0)]
    if rng.random() < 0.02:  # only the last record (0xf9f) free: still "none"
        return [(SITE_BUILDINGS + b * 0x3a, 1 if b < 0xf9f else 0xff, 1) for b in range(1, 0xfa0)]
    used = rng.randrange(0, 20)
    for b in range(1, used + 1):
        fields.append((SITE_BUILDINGS + b * 0x3a, rng.randrange(0, 0xff), 1))
    fields.append((SITE_BUILDINGS + (used + 1) * 0x3a, 0xff, 1))
    fields.append((BUILDING_COUNT, rng.randrange(0, 40), 4))
    return fields


def check_site_state(rng, pool, args):
    x, y, type_, player, mode = args
    fields = []
    r = rng.random()
    if r < 0.06:
        size = rng.randint(8, 15)          # border tiles off the map: "illegal building offset"
    elif r < 0.12:
        size = rng.randint(16, 200)         # anything, often outside the bounds
    else:
        size = max(x, y) + 16 + rng.choice([0, 0, rng.randrange(60)])
    fields.append((MAP_SIZE, size, 4))
    fields += site_player(rng, player)
    owner = 0xff if mode >= 2 else player
    fields += site_window(rng, x, y, owner, flat=rng.random() < 0.25)
    fields += site_offset_table(rng)
    full = rng.random() < 0.08
    for slot in range(30):
        fields.append((SITE_CACHE + (player * 30 + slot) * 962, 1 if full or rng.random() < 0.2 else 0, 1))
    fields += site_buildings(rng)
    fields += [(0x32870, rng.randrange(1000), 4), (0x32874, rng.randrange(1000), 4)]
    return fields


def plan_site_state(rng, pool, args):
    x, y, type_, player, height = args
    fields = [(MAP_SIZE, max(x, y) + rng.choice([-3, 0, 16, 40]), 4)]
    fields += site_player(rng, player)
    flat = rng.random() < 0.25
    # Some flat windows at exactly the planned height: no earth to move
    # (PlanSiteLevelling's early "return 1" for an amount of 0).
    fields += site_window(rng, x, y, player, flat=flat, height=height if flat and rng.random() < 0.3 else None)
    return fields


def add_site_state(rng, pool, args):
    x, y, player, type_ = args
    fields = [(MAP_SIZE, max(x, y) + rng.choice([1, 3, 5, 7, 11, 13, 40]), 4)]
    fields += site_player(rng, player)
    for dx in range(-8, 9):
        for dy in range(-8, 9):
            fields.append((site_tile(x + dx, y + dy) + 0x14, rng.randrange(1, 40), 2))
    fields += site_buildings(rng)
    return fields


SPECS += [
    Spec("CGameData_PlanSiteLevelling", 0x004da440, "thiscall",
         [between(15, 120), between(15, 120), site_type, below(8), lambda rng, pool: rng.randint(0, 140)],
         presets=plan_site_state, max_cases=600),
    Spec("CGameData_AddBuildingSite", 0x004dd280, "thiscall",
         [between(15, 120), between(15, 120), below(20), site_type], presets=add_site_state, max_cases=600),
    # Dead blocks: the size classification after the levelling is entered
    # only with an amount >= 1 (PlanSiteLevelling returns 1 for flat land and
    # mines use 1), so the compare chain's branches for 0 never run; and
    # 0x4d9ece is the path for an empty border list, which no layout in
    # S3.EXE has (checked for all 4 races x 56 types).
    Spec("CGameData_CheckBuildingSite", 0x004d99e0, "thiscall",
         [between(10, 120), between(10, 120), site_type, below(8), one_of(0, 1, 1, 1, 2, 3)],
         presets=check_site_state, max_cases=2500,
         exempt_blocks=(0x4da331, 0x4da34a, 0x4da363, 0x4da37e, 0x4da39b, 0x4da3b8, 0x4da3d5, 0x4da3f2,
                        0x4d9ece)),
]


# Sites for the random start positions (0x5074b0): IsFreeSmallTowerSite
# 0x508420 = CheckBuildingSite(x, y, 0xf, player, 2) != 0, and
# IsGoodStartPosition 0x5081a0, which also counts land and resources over
# the first 9995 offset-table entries around (x, y). The region's densities
# are drawn around the thresholds (half the cases aim at the strictest).

def castle_site_state(rng, pool, args):
    player, x, y = args[:3]
    return check_site_state(rng, pool, [x, y, 0xf, player, 2])


START_OFFSETS = None


def start_offsets():
    global START_OFFSETS
    if START_OFFSETS is None:
        import random as _random
        START_OFFSETS = [(f[0], f[1]) for f in site_offset_table(_random.Random(0), rings=58)
                         if (f[0] - SITE_OFFSETS) % 16 in (0, 4)]
    return START_OFFSETS


def start_position_state(rng, pool, args):
    player, x, y, attempts = args
    fields = check_site_state(rng, pool, [x, y, 0xf, player, 2])
    fields = [f for f in fields if not (SITE_OFFSETS <= f[0] < SITE_OFFSETS + 16 * 2000)]
    fields.append((MAP_SIZE, rng.choice([x + 60, x + 30, 0x300]) if rng.random() < 0.9 else rng.randint(16, 60), 4))
    offsets = start_offsets()
    fields += [(a, v, 4) for a, v in offsets]
    ideal = rng.random() < 0.5
    u = lambda low, high: rng.uniform(low, high)
    grass = 0.9 if ideal else u(0.4, 1.0)
    trees = 0.0083 if ideal else u(0.002, 0.012)
    stones = 0.0038 if ideal else u(0.0005, 0.006)
    unowned = 1.0 if ideal else rng.choice([1.0, 0.985, 0.9, 0.7])
    kinds = [(0x10, 0.008 if ideal else u(0, 0.008)), (0x20, 0.004 if ideal else u(0, 0.004)),
             (0x30, 0.002 if ideal else u(0, 0.002)), (0x40, 0.0012 if ideal else u(0, 0.001)),
             (0x50, 0.0012 if ideal else u(0, 0.001)), (0x60, 0.01)]
    base = rng.randint(20, 60)
    for k in range(9995):
        dx = offsets[2 * k][1]
        dy = offsets[2 * k + 1][1]
        dx = dx - (1 << 32) if dx & 0x80000000 else dx
        dy = dy - (1 << 32) if dy & 0x80000000 else dy
        tx, ty = x + dx, y + dy
        if not (0 <= tx < 0x300 and 0 <= ty < 0x300):
            continue
        t = site_tile(tx, ty)
        site = k < 217 and rng.random() > 0.002
        obj = 0
        if not site:
            if rng.random() < trees:
                obj = 0x44 + rng.randrange(16)
            elif rng.random() < stones:
                obj = 0x73 + rng.randrange(12)
            elif rng.random() < 0.01:
                obj = rng.randrange(256)
        res = rng.randrange(16)
        for kind, p in kinds:
            if rng.random() < p:
                res |= kind
                break
        fields += [(t + 4, base + rng.randrange(2), 1),
                   (t + 6, 0x10 + rng.randrange(4) if site or rng.random() < grass else rng.choice([0x20, 0x30, 0x40, 4, 0x60]), 1),
                   (t + 7, obj, 1), (t + 8, 0xff if site or rng.random() < unowned else rng.randrange(4), 1),
                   (t + 0xa, 0, 1), (t + 2, 0, 2), (t + 0x14, 0, 2), (t + 0x11, res, 1)]
    return fields


def start_attempts(rng, pool):
    tier = rng.randrange(4)
    edges = [0, 29999, 30000, 59999, 60000, 99999, 100000, 250000]
    return edges[2 * tier + rng.randrange(2)] if rng.random() < 0.3 else edges[2 * tier] + rng.randrange(100)


SPECS += [
    Spec("CGameData_IsFreeSmallTowerSite", 0x00508420, "thiscall", [below(8), between(10, 120), between(10, 120)],
         presets=castle_site_state, max_cases=600),
    Spec("CGameData_IsGoodStartPosition", 0x005081a0, "thiscall",
         [below(8), between(15, 400), between(15, 400), start_attempts], presets=start_position_state,
         max_cases=250),
]


# -- batch 29: random map generation (map_generation.cpp, map_regions.cpp,
# world_create.cpp) ------------------------------------------------------------

def terrain_or_mark(rng, pool):
    return rng.choice([0xf3, rng.getrandbits(32)]) if rng.random() < 0.15 else rng.randrange(4)


def small_terrain_map_with_marks(rng, pool, args):
    """Like small_terrain_map, with some 0xf0/0xf3 (growth/edge mark) tiles."""
    fields = small_terrain_map(rng, pool, args)
    n = fields[0][1]
    n = n if n < 0x80000000 else 0
    for x in range(n):
        for y in range(n):
            if rng.random() < 0.1:
                fields.append((tile(x, y) + 0x6, rng.choice([0xf0, 0xf3]), 1))
    return fields


def fresh_world_tables(offsets: int) -> list:
    """What the world constructor leaves: empty tile ring and event rings,
    empty id tables (with their marks), the first `offsets` offset-table
    entries small (random; the in-process tests use the real table)."""
    def presets(rng):
        fields = [(0xe07888, 0, 4), (0xe0788c, 0, 4), (0x32870, 0, 4), (0x32874, 0, 4), (0x366fc, 0, 4)]
        fields += [(offset, 0, 4) for offset in range(0xe114d8, 0xe125e8 + 0x2000, 4)]
        for k in range(offsets):
            reach = 1 if k < 7 else 5 if k < 96 else 12
            fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                       (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randint(-reach, reach) & 0xFFFFFFFF, 4)]
        return fields
    return presets


def region_map(rng, pool, args):
    """A 6..40 map of grass, water (some with harbour objects 0x0d..0x10),
    mountains (class 0x20), blocked tiles and up to four owners; area
    records mostly free."""
    n = rng.randint(6, 40)
    fields = [(MAP_SIZE, n, 4)] + fresh_world_tables(61)(rng)
    fields += [(0xe145e8 + area * 0x2e0, 0 if rng.random() < 0.05 else 0xff, 1) for area in range(64)]
    owners = rng.randrange(5)
    for x in range(n):
        for y in range(n):
            patch = ((x // 5) * 7 + (y // 6) * 3) % 8
            terrain = 0x10 if patch < 4 else 5 + patch % 3 if patch < 6 else 0x20 if patch == 6 else 0x30
            if rng.random() < 0.05:
                terrain = rng.getrandbits(8)
            water = terrain <= 7
            obj = 0x0d + rng.randrange(4) if water and rng.random() < 0.03 else rng.getrandbits(8) if rng.random() < 0.1 else 0
            quarter = (x * 2 // n) * 2 + y * 2 // n
            flags = rng.getrandbits(8) & 0x6e
            if water or rng.random() < 0.07:
                flags |= 1
            t = tile(x, y)
            fields += [(t + 0x6, terrain, 1), (t + 0x7, obj, 1), (t + 0x8, quarter if quarter < owners else 0xff, 1),
                       (t + 0xa, flags, 1), (t + 0x14, 0, 2)]
    return fields


def create_world(rng, pool, args):
    return fresh_world_tables(600)(rng)


SPECS += [
    Spec("CGameData_ExpandTerrain", 0x00519440, "thiscall", [terrain_or_mark, terrain_or_mark],
         presets=small_terrain_map_with_marks, max_cases=300),
    Spec("CGameData_MarkTerrainEdge", 0x0051a2e0, "thiscall", [terrain_or_mark],
         presets=small_terrain_map_with_marks, max_cases=300),
    Spec("CGameData_BuildRegionsAndAreas", 0x0051a580, "thiscall", [], presets=region_map, max_cases=60,
         max_instructions=200_000_000),
    # Whole random maps of 64 x 64 (sizes of the real game are 128..768,
    # far too slow here; the in-process tests run those). About 100 s per
    # case, so quick runs do one case only: the main evidence is the
    # in-process whole-world comparison (world_create.cpp tests) and the
    # random-map whole-game checks (check_random_map.py); --full runs all 10.
    Spec("CGameData_GenerateRandomMap", 0x005166d0, "thiscall",
         [anything, one_of(65, 72, 80, 96, 64, 33),
          lambda rng, pool: rng.choice([0, 0, 0, 1, 2, 3, rng.getrandbits(32)])],
         presets=create_world, max_cases=10, max_instructions=3_000_000_000, quick_cases=1),
]


# Package C, batch 1 (terrain_updates.cpp, economy_helpers.cpp)
def shade_around_heights(rng, pool, args):
    """Heights of the 5x5 block around the tile: the shade of the tile and of
    its six neighbours reads their neighbours too."""
    x, y = args
    wide = rng.random() < 0.25
    base = rng.randrange(256)
    return [(tile(x + dx, y + dy) + 0x4, rng.randrange(256) if wide else (base + rng.randrange(25)) & 0xFF, 1)
            for dx in range(-2, 3) for dy in range(-2, 3)]


PATCH_TERRAINS = (0x10, 0x16, 0x12, 0x13, 0x18, 0x1C)


def terrain16_patch(rng, pool, args):
    """Terrains of the tile and its six neighbours: mostly accepted ones, so
    every neighbour check and the final change are reached."""
    x, y = args
    fields = []
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
        value = rng.choice(PATCH_TERRAINS) if rng.random() < 0.93 else rng.randrange(256)
        fields.append((tile(x + dx, y + dy) + 0x6, value, 1))
    return fields


def pile_claims(rng, pool, args):
    settler, pile = args
    fields = []
    for slot in range(128):
        fields += [(PILE_CLAIMS + slot * 4, pile if rng.random() < 0.5 else rng.randrange(6), 2),
                   (PILE_CLAIMS + slot * 4 + 2, settler if rng.random() < 0.5 else rng.randrange(6), 2)]
    return fields


SPECS += [
    Spec("CGameData_UpdateShadeAround", 0x00519670, "thiscall", [between(2, 380), between(2, 380)],
         eax_mask=0, presets=shade_around_heights, max_cases=1000),
    Spec("CGameData_MakeFieldTile", 0x0051ba00, "thiscall", [between(1, 380), between(1, 380)],
         eax_mask=0, presets=terrain16_patch, max_cases=2000),
    Spec("CGameData_CountOtherPileClaims", 0x00510800, "thiscall", [below(6, 0.1), below(6, 0.1)],
         presets=pile_claims, max_cases=500),
]


# Package A, batch 1: pathfinder leaves (pathing.cpp) and walking helpers
# (settler_walk.cpp). The pathfinder's state (layout::PathState, 0x9c bytes)
# lies in scratch slot 0.
PATH_STATE = SCRATCH
PATH_OUT = SCRATCH + SCRATCH_SLOT_BYTES  # TrackAngle's *last and *turns


def path_state_fields(rng, x=None, y=None):
    """Position, region values (small, so that tiles prepared with
    path_tile_fields match them) and a sector type that is mostly valid."""
    x = rng.randint(4, 379) if x is None else x
    y = rng.randint(4, 379) if y is None else y
    sector = rng.choice([1, 2, 3, 1, 2, 3, 1, 2, 3, 0, 4, rng.getrandbits(32)]) if rng.random() < 0.1 \
        else rng.choice([1, 2, 3])
    return [(PATH_STATE + 0x4, x, 4), (PATH_STATE + 0x8, y, 4),
            (PATH_STATE + 0x54, rng.randrange(2) if rng.random() < 0.9 else rng.getrandbits(32), 4),
            (PATH_STATE + 0x58, rng.randrange(2) if rng.random() < 0.9 else rng.getrandbits(32), 4),
            (PATH_STATE + 0x5c, rng.randrange(2) if rng.random() < 0.9 else rng.getrandbits(32), 4),
            (PATH_STATE + 0x60, sector, 4)]


PATH_JOBS = (1, 0x37, 2, 0x16D, 0xB0, 4, 5, 0x100)


def path_tile_fields(rng, x, y):
    """One tile: regions 0/1, mostly no settler, else a settler with a
    walking, idle or working job."""
    t = tile(x, y)
    settler = 0 if rng.random() < 0.5 else rng.randint(1, 31999)
    fields = [(t + 0x10, rng.randrange(2), 1), (t + 0x14, rng.randrange(2), 2), (t + 0x16, rng.randrange(2), 2),
              (t + 0x0, settler, 2)]
    if settler:
        fields.append((SETTLERS + settler * SETTLER_BYTES + 0xE, rng.choice(PATH_JOBS), 2))
    return fields


def path_tile_case(rng, pool, args):
    x, y = args[1], args[2]
    return path_state_fields(rng) + path_tile_fields(rng, x, y)


def path_delta(rng, pool):
    """Offsets for the direction functions; |d| < 2000 keeps Angle's
    reciprocal index inside the emulated world."""
    pick = rng.random()
    if pick < 0.4:
        return rng.randint(-2, 2) & 0xFFFFFFFF
    if pick < 0.8:
        return rng.randint(-40, 40) & 0xFFFFFFFF
    return rng.randint(-1999, 1999) & 0xFFFFFFFF


def path_sextant(rng, pool, args):
    """A sextant 0..5, sometimes an invalid one (assertion)."""
    value = rng.randrange(6) if rng.random() < 0.9 else rng.choice([6, 7, 0xFFFFFFFF, rng.getrandbits(32)])
    return [(PATH_STATE + 0x44, value, 4)]


def path_choose_sextant(rng, pool, args):
    fields = path_state_fields(rng)
    dx, dy = path_delta(rng, pool), path_delta(rng, pool)
    if rng.random() < 0.03:
        dx = dy = 0
    turn = 1000 if rng.random() < 0.2 else rng.randrange(6)
    fields += [(PATH_STATE + 0xC, dx, 4), (PATH_STATE + 0x10, dy, 4), (PATH_STATE + 0x48, turn, 4)]
    for offset in (0x14, 0x18):  # abs_dx / abs_dy: the near test of sextants 3 and 4
        fields.append((PATH_STATE + offset, rng.randint(-1, 6) & 0xFFFFFFFF, 4))
    return fields


def path_line_step(rng, pool, args):
    fields = path_state_fields(rng)
    a, b = rng.randint(0, 300), rng.randint(0, 300)
    if rng.random() < 0.2:
        a = b
    if a == 0 and b == 0:
        b = 1
    fields += [(PATH_STATE + 0x1C, a, 4), (PATH_STATE + 0x20, b, 4)]
    for offset in (0x34, 0x38, 0x3C, 0x40):
        fields.append((PATH_STATE + offset, rng.randint(-1, 1) & 0xFFFFFFFF, 4))
    return fields


def path_track_angle(rng, pool, args):
    last = 0x7FFFFFFF if rng.random() < 0.2 else rng.choice(
        [rng.randrange(0x30000), 0x50001 + rng.randrange(0x30000), rng.randrange(0x80000)])
    return path_state_fields(rng) + [
        (PATH_STATE + 0x90, 0 if rng.random() < 0.5 else rng.randrange(0x80000), 4),
        (PATH_OUT, last, 4), (PATH_OUT + 4, (rng.randrange(5) * 0x80000 - 0x100000) & 0xFFFFFFFF, 4)]


def traffic_tile(rng, pool, args):
    x, y = args
    traffic = rng.choice([11, 11, 0xFF, 0xFE, 0, rng.randrange(256)])
    fields = [(tile(x, y) + 0x12, traffic, 1)]
    return fields + wear_path_world(rng, pool, args)


SPECS += [
    Spec("CGameData_AddTileTraffic", 0x00503940, "thiscall", [TILE, TILE], presets=traffic_tile,
         max_cases=2000),
    Spec("CGameData_IsWalkingJob", 0x004fa6d0, "thiscall", [below(0x180, 0.3)], max_cases=4000),
    Spec("CGameData_IsStationaryJob", 0x004fa880, "thiscall", [below(0x180, 0.3)], max_cases=4000),
    Spec("Pathing_TileCost", 0x004a36a0, "thiscall",
         [scratch(0), TILE, TILE, below(3), below(3), below(3)], presets=path_tile_case, max_cases=3000),
    Spec("Pathing_SectorTileCost", 0x004a3870, "thiscall", [scratch(0), TILE, TILE],
         presets=path_tile_case, max_cases=2000),
    Spec("Pathing_FreeTileCost", 0x004a3940, "thiscall", [scratch(0), TILE, TILE],
         presets=path_tile_case, max_cases=2000),
    # 0x4a3aa9: the code after the assertion call (it never returns).
    Spec("Pathing_DirectionOfDelta", 0x004a3a40, "thiscall", [path_delta, path_delta], max_cases=3000,
         exempt_blocks=(0x4a3aa9,)),
    Spec("Pathing_DirectionOfStep", 0x004a4b00, "thiscall", [between(-1, 1), between(-1, 1)], max_cases=500),
    # 0x4a48c7: after the assertion call.
    Spec("Pathing_DistanceAlong", 0x004a4860, "thiscall", [scratch(0), path_delta, path_delta],
         presets=path_sextant, max_cases=2000, exempt_blocks=(0x4a48c7,)),
    # 0x4a4af4: after the assertion call.
    Spec("Pathing_Angle", 0x004a49d0, "thiscall", [path_delta, path_delta], max_cases=3000,
         exempt_blocks=(0x4a4af4,)),
    # 0x4a4992: the catch(...) handler (logs and rethrows); only reachable
    # through an exception, i.e. Angle's assertion at (0, 0), which the
    # emulator ends as a throw inside Angle.
    Spec("Pathing_TrackAngle", 0x004a48f0, "thiscall",
         [scratch(0), path_delta, path_delta, scratch(1), lambda rng, pool: WORLD_BASE + PATH_OUT + 4],
         presets=path_track_angle, max_cases=3000, exempt_blocks=(0x4a4992,)),
    # 0x4a25d6: dead (dx < 0 with dy > 0 was already sent to sextant 2).
    Spec("Pathing_ChooseSextant", 0x004a23e0, "thiscall", [scratch(0)], presets=path_choose_sextant,
         max_cases=3000, exempt_blocks=(0x4a25d6,)),
    Spec("Pathing_SetupLineStep", 0x004a3d30, "thiscall", [scratch(0)], presets=path_line_step, max_cases=1000),
]


# Package B, batch 1 (pile_goods_moves.cpp, notifications.cpp, folded_getters.cpp,
# settler_spawn.cpp, miner_jobs.cpp)
def _spiral(entries: int) -> list:
    points = sorted(((dx, dy) for dx in range(-12, 13) for dy in range(-12, 13)),
                    key=lambda p: (hex_distance(*p), p))
    return points[:entries]


SPIRAL = _spiral(400)  # the game's offset table order: nearest first (entries 0..36 = radius 0..3)


def spiral_offsets(entries: int) -> list:
    fields = []
    for k, (dx, dy) in enumerate(SPIRAL[:entries]):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
    return fields


def set_pile_good_state(rng, pool, args):
    """The pile on a tile (area below AREA_LIMIT), its links, the empty-list
    head (often the pile itself) and the head for the new goods type."""
    pile, good = args
    record = PILES + pile * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    area = rng.randrange(AREA_LIMIT)

    def link():
        return 0 if rng.random() < 1 / 3 else rng.randrange(1, 0x1000)
    return [(record + 6, x, 2), (record + 8, y, 2), (tile_area_offset(x, y), area, 2),
            (record + 0xa, link(), 2), (record + 0xc, link(), 2),
            (AREAS + 0xa + (area * 0x170 + (good & 0xff)) * 2, link(), 2),
            (AREAS + area * AREA_BYTES + 0x8, pile if rng.random() < 0.5 else link(), 2)]


def drop_good_state(rng, pool, args):
    """Offset entries 0..90 in the game's order, tiles within radius 6 of
    (x, y): dropped piles (kind 0xfe) of the goods type with some room, some
    all leaving, others; free tiles (no flags 0x0f, settler or object, a
    walk region) with occupied neighbours now and then. Sometimes nothing
    is free (the 10,000-entry limit) or the map is small."""
    good, amount, x, y = args
    n = 384 if rng.random() < 0.85 else rng.randint(1, 40)
    fields = [(MAP_SIZE, n, 4)] + spiral_offsets(91)
    crowded = rng.random() < 0.1
    for dx, dy in SPIRAL[:127]:
        tx, ty = x + dx, y + dy
        if not (0 <= tx < 384 and 0 <= ty < 384):
            continue
        t = tile(tx, ty)
        r = rng.random()
        flags = rng.getrandbits(8) if rng.random() < 0.1 else rng.getrandbits(8) & 0xd0
        settler = 0 if rng.random() < 0.85 else rng.randrange(1, 100)
        obj = 0
        if crowded:
            settler = 1
        if r < 0.3:
            pile = rng.randrange(1, 0x1000)
            obj = pile
            record = PILES + pile * PILE_BYTES
            amt = rng.randrange(9) if rng.random() < 0.9 else rng.getrandbits(8)
            area = rng.randrange(AREA_LIMIT)
            pile_good = good if rng.random() < 0.75 else rng.randrange(36)
            fields += [(record + 0x0, pile_good, 1),
                       (record + 0x1, amt, 1),
                       (record + 0x2, amt if rng.random() < 0.3 else rng.randrange(9), 1),
                       (record + 0x4, 0xfe if rng.random() < 0.8 else rng.choice(PILE_KINDS), 1),
                       (record + 0x6, tx, 2), (record + 0x8, ty, 2),
                       (tile_area_offset(tx, ty), area, 2),
                       (AREAS + 0xa + (area * 0x170 + pile_good) * 2, rng.randrange(0x1000), 2)]
            if rng.random() < 0.9:
                flags &= ~0x20
        elif r < 0.4:
            obj = rng.randrange(1, 0x1000)
            flags |= 0x20
        elif r < 0.55 and not crowded:
            settler = 0
            flags &= 0xf0
        fields += [(t + 0x0, settler, 2), (t + 0x2, obj, 2), (t + 0xa, flags, 1),
                   (t + 0x10, 0 if rng.random() < 0.1 else rng.randrange(1, 20), 1),
                   (t + 0x8, rng.randrange(20), 1)]
    return fields


def drop_amount(rng, pool) -> int:
    return rng.choice([0, 1, 2, 3, 5, 8, 9, 13, 20])


NOTIFICATION_LISTS = 0x2d320
NOTIFICATION_TIMERS = 0x2e5e0
LOCAL_PLAYER = 0x2d2d4
CONFIG_NOTIFICATION_LEVEL = 0x7a7af0 + 0x64
UI_FEEDBACK_COUNT = 0x15394ac


def notification_state(rng, pool, args):
    """Local player mostly the caller; the type's timers: a few running near
    (x, y), the rest free (sometimes all running); the lists mostly with
    room (sometimes full); the UI feedback list sometimes full."""
    player, kind, x, y = args
    fields = [(LOCAL_PLAYER, player if rng.random() < 0.85 else rng.randrange(4), 4),
              (UI_FEEDBACK_COUNT, rng.randrange(11), 4)]
    running = 10 if rng.random() < 0.1 else rng.randrange(4)
    for k in range(10):
        entry = NOTIFICATION_TIMERS + (kind * 10 + k) * 12
        if k < running:
            fields += [(entry, rng.randrange(1, 100), 4),
                       (entry + 4, (x + rng.randint(-60, 60)) & 0xFFFFFFFF, 4),
                       (entry + 8, (y + rng.randint(-60, 60)) & 0xFFFFFFFF, 4)]
        else:
            fields.append((entry, 0 if rng.random() < 0.8 else rng.randrange(1, 100), 4))
    used = 100 if rng.random() < 0.1 else rng.randrange(6)
    for lst in range(-1, 4):
        for k in range(100):
            entry = NOTIFICATION_LISTS + (lst * 100 + k) * 12
            fields.append((entry & 0xFFFFFFFF, rng.randrange(1, 40) if k < used else 0, 4))
    return fields


def notification_level(rng, pool, args):
    return [(CONFIG_NOTIFICATION_LEVEL, rng.choice([0, 1, 3, 5, 7, 8, 9, 10, rng.getrandbits(32)]), 4)]


def spawn_settlers_state(rng, pool, args):
    """Offsets 0..126 in the game's order and tiles within radius 6 of (x, y):
    usable ones (the player's land, no flags 0x0f, no settler) and others;
    at least five usable on the map, so the walk ends inside the table.
    Free settler records and the player's race for CreateSettler."""
    count, kind, player, x, y = args
    n = 384 if rng.random() < 0.8 or max(x, y) >= 50 else rng.randint(max(x, y) + 1, 60)
    fields = [(MAP_SIZE, n, 4), (PLAYER_RACE + player * PLAYER_RECORD_BYTES, rng.randrange(4), 4)]
    fields += spiral_offsets(127)
    usable = 0
    for dx, dy in SPIRAL[:127]:
        tx, ty = x + dx, y + dy
        if not (0 <= tx < n and 0 <= ty < n):
            continue
        good = rng.random() < 0.3
        owner = player if good or rng.random() < 0.5 else rng.randrange(4)
        terrain = rng.randrange(0x10, 0x40) if good or rng.random() < 0.5 else rng.randrange(0x10)
        flags = rng.getrandbits(8) & (0xf0 if good else 0xff)
        settler = 0 if good or rng.random() < 0.5 else rng.randrange(1, 100)
        if owner == player and terrain >= 0x10 and flags & 0xf == 0 and settler == 0:
            usable += 1
        t = tile(tx, ty)
        fields += [(t + 0x8, owner, 1), (t + 0x6, terrain, 1), (t + 0xa, flags, 1), (t + 0x0, settler, 2)]
    if usable < 5:
        for dx, dy in SPIRAL[:127]:
            tx, ty = x + dx, y + dy
            if usable >= 5:
                break
            if 0 <= tx < n and 0 <= ty < n:
                t = tile(tx, ty)
                fields += [(t + 0x8, player, 1), (t + 0x6, 0x10, 1), (t + 0xa, 0, 1), (t + 0x0, 0, 2)]
                usable += 1
    # Free settler records: every fourth of 1..40 in use.
    fields += [(SETTLERS + s * SETTLER_BYTES, rng.randrange(20) if s % 4 == 0 else 0xff, 1) for s in range(1, 41)]
    fields.append((SETTLER_HIGH_WATER, rng.randrange(60), 4))
    return fields


def enter_slot_state(rng, pool, args):
    settler = SETTLERS + args[0] * SETTLER_BYTES
    building = rng.randrange(1, 0x1000)
    record = BUILDINGS + building * BUILDING_BYTES
    bx, by = rng.randrange(1, 370), rng.randrange(1, 380)
    taken = rng.randrange(6)
    fields = [(settler + 0x14, rng.getrandbits(8) & (0xff if rng.random() < 0.25 else 0xbf), 1),
              (settler + 0x22, building, 2),
              (record + 0x2, bx, 2), (record + 0x4, by, 2),
              (record + 0x6, rng.choice([0x0f, 0x10, 0x12, 0x11, 0x05]), 1),
              (record + 0x7, rng.randrange(4) if rng.random() < 0.9 else rng.getrandbits(8), 1)]
    for k in range(1, 8):
        fields.append((tile(bx + k, by) + 0x0, rng.randrange(1, 100) if k <= taken else 0, 2))
    return fields


def seven_tile_state(rng, pool, args):
    kind, player, x, y = args
    fields = [(PLAYER_RACE + player * PLAYER_RECORD_BYTES, rng.randrange(4), 4)]
    fields += [(SETTLERS + s * SETTLER_BYTES, rng.randrange(20) if s % 3 == 0 else 0xff, 1) for s in range(1, 30)]
    fields.append((SETTLER_HIGH_WATER, rng.randrange(40), 4))
    return fields


def gfx_sprite(animation: int, rng) -> list:
    """Presets that make `animation` a valid sprite id of the fake graphics object."""
    group, index = animation >> 16, animation & 0xFFFF
    if group >= 0x130:
        return []
    return [(GFX_OBJECT + 0xb24 + group * 4, WORLD_BASE + GFX_POINTERS, 4),
            (GFX_OBJECT + 0x264 + group * 4, 0, 4),
            (GFX_POINTERS + 8 + index * 4, WORLD_BASE + GFX_HEADERS + rng.randrange(0x100), 4)]


SOLDIER_TYPES = [0x20, 0x23, 0x08, 0x1e, 0x21, 0x0f, 0x1f, 0x22]


def soldier_animation_state(rng, pool, args):
    settler = SETTLERS + args[0] * SETTLER_BYTES
    building = rng.randrange(1, 0x1000)
    record = BUILDINGS + building * BUILDING_BYTES
    kind_type = rng.choice(SOLDIER_TYPES + [0x05, 0x30])
    art = rng.choice([0x10, 0x12, 0x11, 0x0f])
    race = rng.randrange(3) if rng.random() < 0.9 else rng.getrandbits(8)
    settler_race = rng.randrange(4)
    kind = 0x126 + SOLDIER_TYPES.index(kind_type) if kind_type in SOLDIER_TYPES else 0x125
    kind += 9 if art == 0x10 else 0x12 if art == 0x12 else 0
    kind += 0x1b if race == 1 else 0x36 if race == 2 else 0
    return ([(settler + 0x1, kind_type, 1), (settler + 0x2, settler_race, 1), (settler + 0x22, building, 2),
             (record + 0x6, art, 1), (record + 0x7, race, 1)]
            + gfx_sprite(animation_table()[kind * 4 + settler_race], rng))


def saved_animation_state(rng, pool, args):
    settler, kind = args
    race = rng.randrange(4)
    return [(SETTLERS + settler * SETTLER_BYTES + 2, race, 1)] + gfx_sprite(animation_table()[kind * 4 + race], rng)


def gfx_global(rng, pool, args):
    return [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4)]


def slope_state(rng, pool, args):
    x, y = args
    n = rng.choice([384, 384, 384, 0, 1, 2, 3, 100])
    fields = [(MAP_SIZE, n, 4)]
    base = rng.randrange(256)
    if 1 <= x < 383 and 1 <= y < 383:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
            fields.append((tile(x + dx, y + dy) + 0x4, (base + rng.randrange(20)) & 0xFF, 1))
    return fields


def slope_coordinate(rng, pool) -> int:
    return rng.choice([0, 1, 382, 383, 384, rng.randrange(384), rng.randrange(384)])


SPECS += [
    Spec("CGameData_SetPileGood", 0x005061b0, "thiscall",
         [between(1, 0xfff), lambda rng, pool: rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(36)],
         presets=set_pile_good_state, max_cases=2000),
    Spec("CGameData_DropGoodNear", 0x005046b0, "thiscall",
         [below(36), drop_amount, between(0, 383), between(0, 383)], eax_mask=0,
         presets=drop_good_state, max_cases=300),
    Spec("CGameData_NotifyLocalPlayer", 0x004fec10, "thiscall",
         [below(4), below(42), below(384), below(384)], eax_mask=0,
         presets=notification_state, globals=notification_level, max_cases=1500),
    Spec("GetMemberDword64", 0x0049f510, "thiscall", []),
    Spec("GetMemberByte189", 0x00414030, "thiscall", [], eax_mask=0xFF),
    Spec("CGameData_SpawnSettlersAround", 0x0050cb20, "thiscall",
         [between(1, 5), lambda rng, pool: rng.randrange(0x30) if rng.random() < 0.9 else rng.randrange(0x50),
          below(4), lambda rng, pool: rng.randrange(60) if rng.random() < 0.3 else rng.randrange(384),
          lambda rng, pool: rng.randrange(60) if rng.random() < 0.3 else rng.randrange(384)],
         eax_mask=0, presets=spawn_settlers_state, max_cases=300),
    Spec("CGameData_EnterBuildingSlot", 0x0050ccb0, "thiscall", [between(1, 0x7cfe)], eax_mask=0,
         presets=enter_slot_state, max_cases=1000),
    Spec("CGameData_CreateSevenTileUnit", 0x0050dd20, "thiscall",
         [below(0x50), below(4), between(1, 382), between(1, 382)], eax_mask=0,
         presets=seven_tile_state, max_cases=300),
    Spec("CGameData_SetSoldierAnimationInBuilding", 0x0050e140, "thiscall", [between(1, 0x7cfe)],
         presets=soldier_animation_state, globals=gfx_global, max_cases=1000),
    Spec("CGameData_SetSettlerSavedAnimation", 0x0050e320, "thiscall", [below(0x8000), below(0x180)],
         presets=saved_animation_state, globals=gfx_global, max_cases=1000),
    Spec("CGameData_TileSlope", 0x004faa70, "thiscall", [slope_coordinate, slope_coordinate],
         presets=slope_state, max_cases=2000),
]


# Package C, batch 2 (economy.cpp, building_crush.cpp)
def population_world(rng, pool, args):
    """Up to 40 settlers and 40 buildings with owners (sometimes beyond the
    20 players, as in the original's unchecked indexing), carriers and
    residences, and the players' limit bases."""
    settlers, buildings = rng.randrange(40), rng.randrange(40)
    wild = rng.random() < 0.1

    def owner():
        if rng.random() < 0.15:
            return 0xFF
        return rng.randrange(255) if wild and rng.random() < 0.1 else rng.randrange(20)

    fields = [(SETTLER_COUNT, settlers, 4), (BUILDING_COUNT, buildings, 4)]
    for n in range(1, settlers + 1):
        fields += [(SETTLERS + n * 0x40, owner(), 1), (SETTLERS + n * 0x40 + 1, 0 if rng.random() < 0.3 else rng.randrange(0x30), 1)]
    for n in range(1, buildings + 1):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, owner(), 1), (b + 6, rng.choice([0x1a, 0x1b, 0x1c, rng.randrange(0x39)]), 1),
                   (b + 0x21, 0 if rng.random() < 0.75 else rng.randrange(256), 1)]
    for p in range(20):
        base = rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(200)
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xcc, base, 4))
    if rng.random() < 0.3:  # many civilians: surplus carriers
        for p in range(20):
            fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xcc, rng.randrange(3), 4))
    return fields


TOWER_SOLDIER_TYPES = (0x20, 0x23, 8, 0x1E, 0x21, 0xF, 0x1F, 0x22)


def tower_soldier_world(rng, pool, args):
    settler = args[0]
    s = SETTLERS + settler * 0x40
    building = rng.randrange(1, 0x200)
    art = rng.choice(TOWER_SOLDIER_TYPES) if rng.random() < 0.8 else rng.randrange(256)
    btype = rng.choice([0x10, 0x12, 0xF, 0x11]) if rng.random() < 0.8 else rng.randrange(256)
    brace = rng.randrange(4) if rng.random() < 0.9 else rng.randrange(256)
    race = rng.randrange(4)
    fields = [(s + 1, art, 1), (s + 2, race, 1), (s + 0x22, building, 2),
              (BUILDINGS + building * BUILDING_BYTES + 6, btype, 1),
              (BUILDINGS + building * BUILDING_BYTES + 7, brace, 1)]
    kind = 0x176
    if art in TOWER_SOLDIER_TYPES:
        kind = 0x177 + TOWER_SOLDIER_TYPES.index(art)
    kind += 9 if btype == 0x10 else 0x12 if btype == 0x12 else 0
    kind += 0x1B if brace == 1 else 0x36 if brace == 2 else 0
    animation = animation_table()[kind * 4 + race]
    group, index = animation >> 16, animation & 0xFFFF
    fields += [(GFX_OBJECT + 0xb24 + group * 4, WORLD_BASE + GFX_POINTERS, 4),
               (GFX_OBJECT + 0x264 + group * 4, 0, 4),
               (GFX_POINTERS + 8 + index * 4, WORLD_BASE + GFX_HEADERS + rng.randrange(0x100), 4)]
    return fields


SITE_CACHE_BYTES = 0x3c2


def footprint_world(rng, pool, args):
    """The building in args[0]: a real type/race layout, owner and site slot
    in range, mostly buildup 1 (levelling grid), stakes on its tiles."""
    number = args[0]
    b = BUILDINGS + number * BUILDING_BYTES
    x, y = rng.randint(20, 360), rng.randint(20, 360)
    owner, slot = rng.randrange(20), rng.randrange(30)
    fields = [(b, owner, 1), (b + 2, x, 2), (b + 4, y, 2), (b + 6, rng.randint(1, 56), 1), (b + 7, rng.randrange(4), 1),
              (b + 0x21, 1 if rng.random() < 0.6 else rng.randrange(4), 1), (b + 0x22, slot, 2)]
    cache = SITE_CACHE + (owner * 30 + slot) * SITE_CACHE_BYTES
    for k in range(31 * 31):
        fields.append((cache + 1 + k, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1))
    for dx in range(-8, 9):
        for dy in range(-8, 9):
            fields.append((tile(x + dx, y + dy) + 7, 0x71 if rng.random() < 0.5 else rng.randrange(256), 1))
    return fields


SPECS += [
    Spec("CGameData_RecountPlayerPopulation", 0x005104c0, "thiscall", [], eax_mask=0, presets=population_world,
         max_cases=400),
    Spec("CGameData_SetTowerSoldierAnimation", 0x0050e200, "thiscall", [between(1, 0x7cfe)], presets=tower_soldier_world,
         globals=lambda rng, pool, args: [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4)], max_cases=1000),
    # UnblockTile (verified, own spec) is probed: on the lazily random map
    # its region floods would make the cases very slow; the calls and their
    # arguments are compared.
    Spec("CGameData_ClearBuildingFootprint", 0x0051aae0, "thiscall", [between(1, 0xf9f)], eax_mask=0,
         presets=footprint_world, callees={0x0051b770: (2, "thiscall", "probe")}, max_cases=400),
]


# Package A, batch 2: the pathfinder's walking helpers (pathing.cpp). The
# settler argument points at settler record n; the state names the same n.
DIR_X = (1, 1, 0, -1, -1, 0)
DIR_Y = (0, 1, 1, 0, -1, -1)


def settler_number(pointer: int) -> int:
    return (pointer - WORLD_BASE - SETTLERS) // SETTLER_BYTES


def walk_settler(rng, pool):
    return WORLD_BASE + SETTLERS + rng.randint(1, 31999) * SETTLER_BYTES


def walk_case_fields(rng, n):
    """A settler n at (x, y) with a neighbourhood of radius 5 (regions mostly
    0, a third of the tiles taken by settlers with walking, idle or working
    jobs), a matching path state and sometimes a queue of walking settlers
    that leads back to n (two neighbours or a ring of three)."""
    x, y = rng.randint(10, 370), rng.randint(10, 370)
    sextant = rng.randrange(6)
    axis_a = (sextant + 1) % 6
    fields = [(MAP_SIZE, 384 if rng.random() < 0.95 else x + 1, 4),
              (PATH_STATE + 0x0, n, 4), (PATH_STATE + 0x4, x, 4), (PATH_STATE + 0x8, y, 4),
              (PATH_STATE + 0x24, x + rng.randint(-3, 3), 4), (PATH_STATE + 0x28, y + rng.randint(-3, 3), 4),
              (PATH_STATE + 0x34, DIR_X[axis_a], 4), (PATH_STATE + 0x38, DIR_X[sextant], 4),
              (PATH_STATE + 0x3C, DIR_Y[axis_a], 4), (PATH_STATE + 0x40, DIR_Y[sextant], 4),
              (PATH_STATE + 0x1C, 0 if rng.random() < 0.2 else rng.randint(0, 30), 4),
              (PATH_STATE + 0x20, 0 if rng.random() < 0.2 else rng.randint(0, 30), 4),
              (PATH_STATE + 0x44, sextant if rng.random() < 0.97 else rng.randrange(8), 4),
              (PATH_STATE + 0x48, 1000 if rng.random() < 0.2 else rng.randrange(6), 4),
              (PATH_STATE + 0x4C, rng.randint(1, 2), 4), (PATH_STATE + 0x50, rng.randrange(6), 4),
              (PATH_STATE + 0x54, 0, 4), (PATH_STATE + 0x58, 0, 4), (PATH_STATE + 0x5C, 0, 4),
              (PATH_STATE + 0x60, rng.choice([1, 2, 3]), 4)]
    for dx in range(-5, 6):
        for dy in range(-5, 6):
            t = tile(x + dx, y + dy)
            other = 0 if rng.random() < 0.6 else rng.randint(1, 31999)
            fields += [(t + 0x10, 0 if rng.random() < 0.75 else 1, 1), (t + 0x14, 0 if rng.random() < 0.75 else 1, 2),
                       (t + 0x16, 0 if rng.random() < 0.75 else 1, 2), (t + 0x6, rng.randrange(256), 1),
                       (t + 0x0, other, 2)]
            if other:
                record = SETTLERS + other * SETTLER_BYTES
                fields += [(record + 0xE, rng.choice(PATH_JOBS), 2), (record + 0x2E, rng.randrange(6), 1)]
    wdx, wdy = rng.randint(-9, 9), rng.randint(-9, 9)
    if wdx == 0 and wdy == 0:
        wdx = 1
    record = SETTLERS + n * SETTLER_BYTES
    fields += [(record + 0x4, x, 2), (record + 0x6, y, 2), (record + 0x28, x + wdx, 2), (record + 0x2A, y + wdy, 2),
               (record + 0x2C, rng.randrange(256), 1), (record + 0x2D, rng.randrange(5), 1),
               (record + 0x2E, rng.randrange(6), 1), (record + 0xE, 2, 2), (tile(x, y), n, 2)]
    queue, d = rng.randrange(5), rng.randrange(6)
    a, b = rng.randint(1, 31999), rng.randint(1, 31999)
    if queue == 1 and a != n:
        t = tile(x + DIR_X[d], y + DIR_Y[d])
        fields += [(t, a, 2), (t + 0x14, 0 if rng.random() < 0.75 else 1, 2),
                   (SETTLERS + a * SETTLER_BYTES + 0xE, 2, 2),
                   (SETTLERS + a * SETTLER_BYTES + 0x2C, rng.randrange(256) | 0x20, 1),
                   (SETTLERS + a * SETTLER_BYTES + 0x2E, (d + 3) % 6, 1),
                   (SETTLERS + a * SETTLER_BYTES + 0x2D, rng.choice([0, 1, 1, 2]), 1)]
    elif queue == 3:
        # The settler itself on the neighbour tile (a queue of length 1).
        fields.append((tile(x + DIR_X[d], y + DIR_Y[d]), n, 2))
    elif queue == 2 and len({a, b, n}) == 3:
        ax, ay = x + DIR_X[d], y + DIR_Y[d]
        d2 = (d + 2) % 6
        fields += [(tile(ax, ay), a, 2), (SETTLERS + a * SETTLER_BYTES + 0xE, 0x16D, 2),
                   (SETTLERS + a * SETTLER_BYTES + 0x2C, rng.randrange(256) | 0x20, 1),
                   (SETTLERS + a * SETTLER_BYTES + 0x2E, d2, 1),
                   (tile(ax + DIR_X[d2], ay + DIR_Y[d2]), b, 2), (SETTLERS + b * SETTLER_BYTES + 0xE, 2, 2),
                   (SETTLERS + b * SETTLER_BYTES + 0x2C, rng.randrange(256) | 0x20, 1),
                   (SETTLERS + b * SETTLER_BYTES + 0x2E, (d + 4) % 6, 1)]
    return fields


def walk_state_only(rng, pool, args):
    return walk_case_fields(rng, rng.randint(1, 31999))


def walk_with_settler(rng, pool, args):
    return walk_case_fields(rng, settler_number(args[0]))


SPECS += [
    Spec("Pathing_StepOut", 0x004a2ec0, "thiscall", [scratch(0)], presets=walk_state_only, max_cases=1500),
    Spec("Pathing_CompareEdges", 0x004a2f70, "thiscall", [walk_settler, scratch(0)], presets=walk_with_settler,
         max_cases=1500),
    Spec("Pathing_StartEdgeWalk", 0x004a2e20, "thiscall", [walk_settler, scratch(0)], presets=walk_with_settler,
         max_cases=1500),
    Spec("Pathing_ResolveBlocker", 0x004a33d0, "thiscall", [scratch(0), below(6)], presets=walk_state_only,
         max_cases=2000),
    Spec("Pathing_TryStep", 0x004a2c70, "thiscall", [scratch(0)], presets=walk_state_only, max_cases=1500),
    Spec("Pathing_ChooseStepByCost", 0x004a2660, "thiscall", [scratch(0)], presets=walk_state_only, max_cases=1500),
    Spec("Pathing_FollowEdge", 0x004a3170, "thiscall", [walk_settler, scratch(0)], presets=walk_with_settler,
         max_cases=1500),
    Spec("Pathing_WalkLine", 0x004a22f0, "thiscall", [walk_settler, scratch(0)], presets=walk_with_settler,
         max_cases=1500),
]


# Package B, batch 2 (map_effect_spawns.cpp)
def linked_effects(rng, pool, args):
    """Slots 0..used+12 with consistent cell lists (cells 0..7), kinds 0..2
    and ids 0..2 / -1 / -2 (word +0x3c), some free records; 3 blocks per row."""
    used = rng.randrange(40, 60)
    last = used + 12
    heads = [0] * 10
    records = {}
    for slot in range(last + 1):
        free = slot == 0 or rng.random() < 1 / 6
        record = {"tick": 0 if free else rng.randrange(1, 0x10000), "kind": rng.randrange(3),
                  "id": rng.choice([0, 1, 2, 0xfffe, 0xffff]), "list": 0, "prev": 0, "next": 0}
        if not free:
            cell = rng.randrange(8)
            record["list"], record["next"] = cell, heads[cell]
            if heads[cell]:
                records[heads[cell]]["prev"] = slot
            heads[cell] = slot
        records[slot] = record
    fields = [(EFFECT_COUNT, used, 4), (0x668a4, 3, 4)]
    for slot, r in records.items():
        base = slot * 0x4c
        fields += [(base, r["tick"], 4), (base + 0x3a, r["kind"], 1), (base + 0x3c, r["id"], 2),
                   (base + 0x40, r["list"], 4), (base + 0x44, r["prev"], 4), (base + 0x48, r["next"], 4)]
    fields += [(EFFECT_HEADS + cell * 4, heads[cell] if cell < 10 else 0, 4) for cell in range(1400)]
    return fields


def flight_table(rng, pool, args):
    """The usual effect table, with 1..10 blocks per row so that positions up
    to y = 13000 stay in the first 1400 cells."""
    fields = effect_table(0.0)(rng, pool, args)
    return [f for f in fields if f[0] != 0x668a4] + [(0x668a4, rng.randint(1, 10), 4)]


def flight_x(rng, pool) -> int:
    return 3000 + 20 * rng.randrange(60) if rng.random() < 0.3 else rng.randrange(7000)


def flight_y(rng, pool) -> int:
    return 6000 if rng.random() < 0.3 else rng.randrange(6000, 13000)


def short_x(rng, pool) -> int:
    return 3000 + 20 * rng.randrange(60) if rng.random() < 0.3 else rng.randrange(3000, 4500)


def short_y(rng, pool) -> int:
    return 6000 if rng.random() < 0.3 else rng.randrange(6000, 7500)


FLIGHT_ARGS = [anything, flight_x, flight_y, coordinate, coordinate, flight_x, flight_y, coordinate, coordinate]
SHORT_ARGS = [anything, short_x, short_y, coordinate, coordinate, short_x, short_y, coordinate, coordinate]

SPECS += [
    Spec("MapEffect_Reset", 0x00543fd0, "thiscall", []),
    Spec("MapEffects_Unlink", 0x00545e40, "thiscall", [between(1, 52)], presets=linked_effects, max_cases=300),
    Spec("MapEffects_RemoveMatching", 0x00545c80, "thiscall",
         [below(3, 0.05), one_of(0, 1, 2, 0xfffffffe, 0xffffffff),
          lambda rng, pool: 0xFFFFFFFF if rng.random() < 0.3 else rng.randrange(768),
          lambda rng, pool: 0xFFFFFFFF if rng.random() < 0.3 else rng.randrange(512)],
         eax_mask=0, presets=linked_effects, max_cases=300),
    Spec("MapEffects_SpawnArrow", 0x00545150, "thiscall", FLIGHT_ARGS, presets=flight_table,
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
    Spec("MapEffects_SpawnProjectile1", 0x005453f0, "thiscall", FLIGHT_ARGS, presets=flight_table,
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
    Spec("MapEffects_SpawnProjectile2", 0x00545730, "thiscall", FLIGHT_ARGS, presets=flight_table,
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
    Spec("MapEffects_SpawnProjectile0", 0x005458d0, "thiscall", SHORT_ARGS, presets=flight_table,
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
    Spec("MapEffects_SpawnProjectile", 0x00545b90, "thiscall",
         [anything, lambda rng, pool: rng.choice([0, 1, 2, 3, 4, rng.getrandbits(32)])] + SHORT_ARGS[1:],
         presets=flight_table, ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
    Spec("MapEffects_Spawn_544630", 0x00544630, "thiscall",
         [anything, below(4), below(2)] + [coordinate] * 5, presets=effect_table(0.0),
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=200),
    Spec("MapEffects_Spawn_5449f0", 0x005449f0, "thiscall",
         [anything, below(4), below(4)] + [coordinate] * 5, presets=effect_table(0.0),
         ignore_bytes=EFFECT_TABLE_PADDING, max_cases=300),
]


def miner_state(rng, pool, args):
    settler = SETTLERS + args[0] * SETTLER_BYTES
    building = rng.randrange(0x1000)
    art = rng.choice([7, 8, 9, 0x1d, 0x25, 0x38, 6, 0x0a, 0x39, rng.getrandbits(8)])
    return [(settler + 0x22, building, 2), (settler + 0x4, rng.randrange(384), 2),
            (settler + 0x6, rng.randrange(384), 2), (BUILDINGS + building * BUILDING_BYTES + 0x6, art, 1)]


SPECS += [
    Spec("CGameData_MinerJobEnter", 0x004f9a00, "thiscall", [below(0x7d00)], presets=miner_state, max_cases=1000),
]


# Package C, batch 3 (territory.cpp, building_crush.cpp). ResetSettlerToIdle
# 0x50cf50 and TakeGoodFromPile 0x505820 belong to package B (not
# reimplemented yet, they need a consistent world), BlockTile 0x51b6f0 and
# SplitAreaAtTile 0x4e5d50 (verified, own specs) flood regions over the
# lazily random map: all four are probed, their calls compared.
SETTLER_PROBES = {0x0050cf50: (1, "thiscall", "probe"), 0x00505820: (1, "thiscall", "probe"),
                  0x0051b6f0: (2, "thiscall", "probe"), 0x004e5d50: (2, "thiscall", "probe")}
PACKAGE_C_JOBS = (0x0a, 0x0b, 0xa9, 0x4c, 0x4d, 0x4e, 0x4f, 0x68, 0x69, 0x133, 0x134, 0x1)


def settler_scan(rng, pile, px, py, owner=None):
    """Settlers 1..count with jobs, owners, targets and buildings around a pile/tile."""
    count = rng.randrange(30)
    fields = [(SETTLER_COUNT, count, 4)]
    for n in range(1, count + 1):
        s = SETTLERS + n * 0x40
        job = rng.choice(PACKAGE_C_JOBS) if rng.random() < 0.8 else rng.randrange(0x195)
        o = 0xFF if rng.random() < 0.15 else (owner if owner is not None and rng.random() < 0.5 else rng.randrange(8))
        fields += [(s, o, 1), (s + 1, rng.randrange(0x30), 1), (s + 0xE, job, 2),
                   (s + 0x22, pile if rng.random() < 0.5 else rng.randrange(100), 2),
                   (s + 0x24, px if rng.random() < 0.7 else rng.randrange(384), 2),
                   (s + 0x26, py if rng.random() < 0.7 else rng.randrange(384), 2)]
    return fields


def pile_record(rng, number, x, y):
    p = PILES + number * PILE_BYTES
    return [(p, 0xFF if rng.random() < 0.2 else rng.randint(1, 35), 1),
            (p + 1, 0 if rng.random() < 0.25 else rng.randrange(9), 1),
            (p + 2, 0 if rng.random() < 0.3 else rng.randrange(256), 1),
            (p + 3, 0 if rng.random() < 0.3 else rng.randrange(9), 1),
            (p + 4, rng.choice([0, 0xFE, 0xFD, 0xFF, 1]) if rng.random() < 0.8 else rng.randrange(256), 1),
            (p + 6, x, 2), (p + 8, y, 2),
            (p + 0xA, 0 if rng.random() < 0.5 else rng.randint(101, 150), 2),
            (p + 0xC, 0 if rng.random() < 0.5 else rng.randint(101, 150), 2)]


def drop_pile_world(rng, pool, args):
    number = args[0]
    x, y = rng.randint(10, 370), rng.randint(10, 370)
    fields = pile_record(rng, number, x, y)
    fields += [(tile(x, y) + 2, number, 2), (tile(x, y) + 0x14, rng.randint(1, 50), 2),
               (MAP_SIZE, 384, 4), (BUILDING_COUNT, rng.randrange(40), 4), (PILE_COUNT, 160, 4)]
    for _ in range(3):
        free = rng.randint(1, 160)
        if free != number:
            fields.append((PILES + free * PILE_BYTES, 0, 1))
    fields += settler_scan(rng, number, x, y)
    return fields


def owned_tile_world(rng, pool, args):
    """The tile (x, y) with a settler, owner, area kind (none / water / land)
    and a pile; the settlers scanning for that pile."""
    x, y = args
    number = 0 if rng.random() < 0.1 else rng.randint(1, 100)
    owner = 0xFF if rng.random() < 0.12 else rng.randrange(8)
    kind = rng.randrange(8)
    area = 0 if kind == 0 else 0x8000 + rng.randrange(0x8000) if kind == 1 else rng.randint(1, 50)
    t = tile(x, y)
    settler = 0 if rng.random() < 0.2 else rng.randint(1, 30)
    px = x if rng.random() < 0.8 else rng.randrange(384)
    py = y if rng.random() < 0.8 else rng.randrange(384)
    fields = [(t, settler, 2), (t + 2, number, 2), (t + 8, owner, 1), (t + 0x14, area, 2),
              (t + 0xA, rng.getrandbits(8) & (0xDF if rng.random() < 0.5 else 0xFF), 1)]
    fields += pile_record(rng, number, px, py)
    if rng.random() < 0.7:
        fields.append((PILES + number * PILE_BYTES + 4, 0xFE, 1))
    fields += settler_scan(rng, number, px, py, owner)
    if settler and rng.random() < 0.5:  # a civilian of the tile's owner
        fields += [(SETTLERS + settler * 0x40, owner, 1), (SETTLERS + settler * 0x40 + 1, 0, 1)]
    return fields


def block_tile_world(rng, pool, args):
    x, y = args
    fields = [(MAP_SIZE, 384, 4), (tile(x, y), 0 if rng.random() < 0.3 else rng.randint(1, 30), 2)]
    fields += site_offset_table(rng, rings=4)
    for dx in range(-4, 5):
        for dy in range(-4, 5):
            if dx or dy:
                fields += [(tile(x + dx, y + dy), 0 if rng.random() < 0.5 else rng.randint(1, 30), 2),
                           (tile(x + dx, y + dy) + 0x10, 0 if rng.random() < 0.25 else rng.randint(1, 200), 1)]
    fields += settler_scan(rng, 0, x, y)
    return fields


def neighbour(x, y, direction):
    dx = 1 if direction in (0, 1) else -1 if direction in (3, 4) else 0
    dy = 1 if direction in (1, 2) else -1 if direction in (4, 5) else 0
    return (x + dx) & 0xFFFFFFFF, (y + dy) & 0xFFFFFFFF


def block_line_world(rng, pool, args):
    number = args[0]
    b = BUILDINGS + number * BUILDING_BYTES
    direction = 0xFFFF if rng.random() < 0.12 else rng.randrange(6)
    wx, wy = rng.randint(30, 350), rng.randint(30, 350)
    fields = [(b + 0x21, 0 if rng.random() < 0.8 else rng.randrange(256), 1), (b + 0x22, direction, 2),
              (b + 0x30, wx, 2), (b + 0x32, wy, 2), (MAP_SIZE, 384, 4)]
    fields += site_offset_table(rng, rings=4)
    if direction != 0xFFFF:
        x, y = wx, wy
        length = rng.randrange(8)
        for k in range(length + 1):
            terrain = 0x30 if k == length else (0x10 if rng.random() < 0.5 else rng.randrange(0x30))
            fields += [(tile(x, y) + 6, terrain, 1), (tile(x, y), 0 if rng.random() < 0.5 else rng.randint(1, 30), 2)]
            x, y = neighbour(x, y, direction)
    fields += settler_scan(rng, 0, wx, wy)
    return fields


SPECS += [
    Spec("CGameData_ResetCivilianOnTile", 0x0050e980, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
    Spec("CGameData_LosePileOnTile", 0x00504f70, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
    Spec("CGameData_ClearTileOwner", 0x0051b640, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
    Spec("CGameData_BlockTileAndMoveSettler", 0x004dbb10, "thiscall", [between(20, 360), between(20, 360)],
         eax_mask=0, presets=block_tile_world, callees=SETTLER_PROBES, max_cases=600),
    Spec("CGameData_BlockBuildingLine", 0x004dbb90, "thiscall", [between(1, 50)], eax_mask=0,
         presets=block_line_world, callees=SETTLER_PROBES, max_cases=600),
    Spec("CGameData_DropInputPile", 0x00504a00, "thiscall", [between(1, 100)], eax_mask=0,
         presets=drop_pile_world, callees=SETTLER_PROBES, max_cases=600),
    Spec("CGameData_DropStoragePile", 0x00504b70, "thiscall", [between(1, 100)], eax_mask=0,
         presets=drop_pile_world, callees=SETTLER_PROBES, max_cases=600),
    Spec("CGameData_DropOutputPile", 0x00504e20, "thiscall", [between(1, 100)], eax_mask=0,
         presets=drop_pile_world, callees=SETTLER_PROBES, max_cases=600),
]


# Package C, batch 4 (economy.cpp): RequestSettlerForBuilding 0x4dacd0 and
# DispatchPileCarrier 0x5052c0 on an area with well-formed settler and pile
# lists (random next pointers would loop). NotifyLocalPlayer 0x4fec10
# (package B, verified, own spec; reads the registry config object) is
# probed, its calls compared. SetPileGood 0x5061b0 runs (original code on
# both sides): DispatchPileCarrier passes the good with garbage in the upper
# three bytes (only CL is loaded; SetPileGood uses only the low byte), so
# its effects are compared instead of its argument.
ECONOMY_PROBES = {0x004fec10: (4, "thiscall", "probe")}
ECONOMY_GOODS = (0xC, 0xD, 0xE, 0xF, 0x10, 0x11, 0x20, 3, 7)
ECONOMY_TYPES = (1, 2, 3, 4, 6, 7, 9, 0xA, 0xB, 0xC, 0xE, 0x10, 0x11, 0x12, 0x13, 0x14, 0x1D, 0x26, 0x2E, 5, 8, 0xF, 0)


def chain(base, size, next_offset, prev_offset, members):
    fields = []
    for k, n in enumerate(members):
        r = base + n * size
        fields += [(r + prev_offset, members[k - 1] if k else 0, 2),
                   (r + next_offset, members[k + 1] if k + 1 < len(members) else 0, 2)]
    return fields, (members[0] if members else 0)


def economy_world(rng, building, type_, pile, no_idle=0.0):
    """`no_idle`: chance that no settler of `type_` is idle (RequestSettlerForBuilding
    then converts a carrier)."""
    area = rng.randint(1, 30)
    idle_ok = rng.random() >= no_idle
    a = AREAS + area * AREA_BYTES
    base = rng.randint(40, 340)

    def nearby():
        return base + rng.randrange(20)

    fields = [(MAP_SIZE, 384, 4), (SETTLER_COUNT, 30, 4), (BUILDING_COUNT, 50, 4), (PILE_COUNT, 30, 4)]
    owner = rng.randrange(3)
    typed, carriers = [], []
    for n in range(1, 31):
        which = rng.randrange(2 if rng.random() < 0.25 else 3)
        if which == 1:
            carriers.append(n)
        elif which == 2 and type_ != 0 and idle_ok:
            typed.append(n)
        s = SETTLERS + n * 0x40
        x, y = nearby(), nearby()
        art = 0 if which == 1 else type_ if which == 2 else rng.randrange(0x30)
        flags = (0x80 | rng.randrange(256)) if rng.random() < 0.25 else rng.randrange(0x80)
        fields += [(s, rng.randrange(3), 1), (s + 1, art, 1), (s + 4, x, 2), (s + 6, y, 2), (s + 0x14, flags, 1),
                   (tile(x, y), n, 2), (tile(x, y) + 0x14, area, 2)]
    if rng.random() < 0.33:
        carriers = carriers[:len(carriers) * rng.randrange(4) // 4]
    goods_members = {g: [] for g in ECONOMY_GOODS}
    empty = []
    for q in range(1, 31):
        g = rng.choice(ECONOMY_GOODS)
        where = rng.randrange(4)
        if where < 2 and q != pile:
            goods_members[g].append(q)
        elif where == 2 and q != pile:
            empty.append(q)
        amount = rng.randrange(9)
        leaving = amount if rng.random() < 0.33 else rng.randrange(amount + 1)
        p = PILES + q * PILE_BYTES
        x, y = nearby(), nearby()
        fields += [(p, g if rng.random() < 0.8 else rng.randrange(36), 1), (p + 1, amount, 1), (p + 2, leaving, 1),
                   (p + 3, rng.randrange(9 - amount), 1), (p + 4, rng.choice([0xFF, 0xFE, 0xFD, 1, 0x20, 0x32, 0x33]), 1),
                   (p + 6, x, 2), (p + 8, y, 2), (tile(x, y) + 0x14, area, 2)]
    f, head = chain(SETTLERS, 0x40, 0x10, 0x12, typed)
    fields += f + [(a + 0x12A + type_ * 2, head, 2)]
    f, head = chain(SETTLERS, 0x40, 0x10, 0x12, carriers)
    fields += f + [(a + 0x12A, head, 2)]
    for g, members in goods_members.items():
        f, head = chain(PILES, PILE_BYTES, 0xC, 0xA, members)
        fields += f + [(a + 0xA + g * 2, head, 2)]
    f, head = chain(PILES, PILE_BYTES, 0xC, 0xA, empty)
    fields += f + [(a + 8, head, 2), (a, owner, 1)]
    for k in range(3):
        share = rng.choice([0, 1, 5, 10, 20, 40]) if rng.random() < 0.75 else rng.randrange(256)
        fields.append((a + 0x23E + k, share, 1))
    pile_good = rng.choice(ECONOMY_GOODS)
    fields.append((a + 0x21A + pile_good, 0 if rng.random() < 0.12 else rng.randint(1, 255), 1))
    b = BUILDINGS + building * BUILDING_BYTES
    dx, dy = nearby(), nearby()
    fields += [(b, owner, 1), (b + 2, nearby(), 2), (b + 4, nearby(), 2), (b + 0xA, dx, 2), (b + 0xC, dy, 2),
               (tile(dx, dy) + 0x14, area, 2), (PILES + pile * PILE_BYTES, pile_good, 1)]
    px, py = nearby(), nearby()
    fields += [(PILES + pile * PILE_BYTES + 6, px, 2), (PILES + pile * PILE_BYTES + 8, py, 2), (tile(px, py) + 0x14, area, 2)]
    return fields


def request_world(rng, pool, args):
    return economy_world(rng, args[0], args[1], 1, no_idle=0.7)


def dispatch_world(rng, pool, args):
    return economy_world(rng, 1, 1, args[0])


def economy_type(rng, pool):
    return rng.choice(ECONOMY_TYPES) if rng.random() < 0.9 else rng.randrange(0x30)


SPECS += [
    # 0x4db404: dead (the tool 0xc is only needed by type 1, and "no pile of
    # tool 0xc" notifies only for other types).
    Spec("CGameData_RequestSettlerForBuilding", 0x004dacd0, "thiscall", [between(1, 50), economy_type],
         presets=request_world, callees=ECONOMY_PROBES, max_cases=1500, exempt_blocks=(0x4db404,)),
    Spec("CGameData_DispatchPileCarrier", 0x005052c0, "thiscall", [between(1, 30)], eax_mask=0,
         presets=dispatch_world, callees=ECONOMY_PROBES, max_cases=1500),
]


# Package C, batch 5: BuildingDone 0x4dbc50 (building_done.cpp). UnblockTile
# (verified, own spec) is probed (region floods on the lazily random map);
# CreatePile, CreateSettler, AllocBuildingExtra, QueuePendingEvent and
# RemovePileFromTile run. Races above 3 only for military types (elsewhere
# they index past the race tables, which the game never does).
MILITARY_TYPES = (0xF, 0x10, 0x12)


def building_done_world(rng, pool, args):
    number = args[0]
    b = BUILDINGS + number * BUILDING_BYTES
    type_ = rng.randrange(0x39) if rng.random() < 0.85 else rng.choice(MILITARY_TYPES)
    owner = rng.randrange(4)
    x, y = rng.randint(30, 350), rng.randint(30, 350)
    pile0, pile1 = rng.randint(1, 250), rng.randint(1, 250)
    fields = [(MAP_SIZE, 384, 4), (b, owner, 1), (b + 6, type_, 1), (b + 2, x, 2), (b + 4, y, 2),
              (b + 0xA, x + rng.randrange(5) - 2, 2), (b + 0xC, y + rng.randrange(5), 2),
              (b + 0xE, rng.randrange(40) if type_ == 1 else rng.getrandbits(16), 2),
              (b + 0x10, pile0, 2), (b + 0x12, pile1, 2),
              (PILES + pile0 * PILE_BYTES + 6, rng.randint(20, 360), 2), (PILES + pile0 * PILE_BYTES + 8, rng.randint(20, 360), 2),
              (PILES + pile1 * PILE_BYTES + 6, rng.randint(20, 360), 2), (PILES + pile1 * PILE_BYTES + 8, rng.randint(20, 360), 2),
              (0x32870, rng.randrange(1000), 4), (0x32874, rng.randrange(1000), 4)]
    for p in range(4):
        race = rng.randrange(8) if type_ in MILITARY_TYPES and rng.random() < 0.2 else rng.randrange(4)
        fields += [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, race, 4),
                   (PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 8, rng.getrandbits(32), 4)]
    for tx in range(x - 12, x + 13):
        for ty in range(y - 12, y + 13):
            fields.append((tile(tx, ty) + 0x14, rng.randint(1, 50), 2))
    return fields


SPECS += [
    Spec("CGameData_BuildingDone", 0x004dbc50, "thiscall", [between(1, 50)], eax_mask=0,
         presets=building_done_world, callees={0x0051b770: (2, "thiscall", "probe")}, max_cases=3000),
]


# Package B, batch 3 (settler_idle.cpp, pile_goods_moves.cpp, miner_jobs.cpp)
IDLE_JOBS = [0x37, 0x35, 0xb0, 0x8e, 0xd4, 0xd5, 0x12e, 0x12f, 1, 2, 0x68, 0x69, 3, 4, 0x8a, 0x1c, 0x2a, 0xe5,
             0x168, 5, 6, 7, 8, 9, 0xe, 0xf, 0x11, 0x12, 0x133, 0x134, 0xa, 0xb, 0xa9, 0x4c, 0x4d, 0x4e, 0x4f,
             0xaa, 0xc, 0xd, 0x50, 0xac, 0x100]
IDLE_TYPES = [0, 1, 2, 0x16, 0x28, 0x0f, 0x20, 0x08, 0x1e, 0x05, 0x0a, 0x30]


def pile_on_tile(rng, pile, area_limit=AREA_LIMIT):
    """A pile record with a position on the map and an area below the limit."""
    record = PILES + pile * PILE_BYTES
    x, y = rng.randrange(5, 379), rng.randrange(5, 379)
    return [(record + 0x6, x, 2), (record + 0x8, y, 2), (tile_area_offset(x, y), rng.randrange(area_limit), 2),
            (record + 0xa, rng.randrange(0x1000), 2), (record + 0xc, rng.randrange(0x1000), 2)]


def neighbour_offsets(rng):
    """Offset entries 0..90 in the game's order (DropGoodNear, PlaceSettlerNear,
    PileNeighbourBuildingByteE)."""
    return spiral_offsets(91)


WORK_JOBS = [3, 4, 0x8a, 0x68, 0x69, 0x1c, 0x1d, 0x20, 0x21, 0x22, 0x23, 0x2a, 0x2b, 0x2d, 0x2e, 0x2f, 0x30, 0x90,
             0x91, 0x92, 0x93, 0x96, 0x97, 0x98, 0x9c, 0xb5, 0xb2, 0xbb, 0xbc, 0xe5, 0xe6, 0xe7, 0xe8, 0xeb, 0xec,
             0xed, 0xf1, 0xf6, 0xf7, 0xf8, 0xf9, 0xfc, 0xfd, 0xfe, 0x102, 0x165, 0x168]


def idle_state(rng, pool, args):
    """A settler in one of the jobs ResetSettlerToIdle handles; half the
    cases aim at one step (building worker counts and bit sets, input
    piles under construction); tiles around it mostly free (DropGoodNear,
    PlaceSettlerNear)."""
    settler = args[0]
    record = SETTLERS + settler * SETTLER_BYTES
    job = rng.choice(IDLE_JOBS)
    art = rng.choice(IDLE_TYPES)
    saved = rng.randrange(5)
    unk18 = rng.randrange(4)
    good = rng.randrange(36)
    buildup = 0 if rng.random() < 0.5 else rng.randrange(1, 4)
    scenario = rng.randrange(8)
    if scenario == 0:
        job, art, saved = rng.choice([3, 4, 0x8a]), 0, rng.randrange(1, 4)
    elif scenario == 1:
        job, art = rng.choice(WORK_JOBS), rng.choice([1, 2])
    elif scenario == 2:
        job, art, unk18 = rng.choice([0x68, 0x69]), rng.choice([0, 3, 4]), rng.randrange(1, 3)
    elif scenario == 3:
        job = rng.choice([5, 6, 7, 8, 9, 0xe, 0xf, 0x11, 0x12])
    elif scenario == 4:
        job, good, buildup = rng.choice([0xa, 0xb, 0xa9]), rng.randrange(1, 3), rng.randrange(1, 4)
    x, y = rng.randrange(10, 370), rng.randrange(10, 370)
    tx, ty = x + rng.randint(-2, 2), y + rng.randint(-2, 2)
    building = rng.randrange(1, 0x1000)
    b = BUILDINGS + building * BUILDING_BYTES
    area = 0 if rng.random() < 0.15 else rng.randrange(1, AREA_LIMIT)
    slot = 0 if rng.random() < 0.3 else rng.randrange(1, 36)
    pile_word = rng.randrange(1, 0x1000) if 0x4c <= job <= 0x4f else building
    target_pile = rng.randrange(1, 0x1000)
    fields = [(MAP_SIZE, 384, 4)] + neighbour_offsets(rng)
    # Tiles around (x, y) for DropGoodNear and PlaceSettlerNear: mostly free.
    for dx, dy in SPIRAL[:127]:
        t = tile(x + dx, y + dy)
        free = rng.random() < 0.6
        fields += [(t + 0x0, 0 if free else rng.randrange(1, 100), 2), (t + 0x2, 0, 2),
                   (t + 0xa, rng.getrandbits(8) & (0xd0 if free else 0xff), 1),
                   (t + 0x10, rng.randrange(1, 4) if free else 0, 1)]
    # Free pile records for CreatePile.
    fields += [(PILES + p * PILE_BYTES, 0 if p % 5 == 0 else 1, 1) for p in range(1, 20)]
    fields += [(record + 0xe, job, 2), (record + 0x1, art, 1), (record + 0x0, rng.randrange(4), 1),
               (record + 0x4, x, 2), (record + 0x6, y, 2), (record + 0x24, tx, 2), (record + 0x26, ty, 2),
               (record + 0x22, pile_word, 2), (record + 0x15, slot, 1), (record + 0x1a, good, 1),
               (record + 0x20, saved, 2), (record + 0x17, rng.randrange(2), 1), (record + 0x18, unk18, 1),
               (record + 0x8, rng.getrandbits(32) if rng.random() < 0.3 else rng.randrange(6), 4),
               (record + 0x10, 0 if rng.random() < 0.3 else rng.randrange(1, 0x3000), 2),
               (record + 0x12, 0 if rng.random() < 0.3 else rng.randrange(1, 0x3000), 2),
               (tile_area_offset(x, y), area, 2),
               (tile(x, y) + 0x0, settler if rng.random() < 0.75 else rng.randrange(1, 0x3000), 2),
               (tile(x, y) + 0x10, 0 if rng.random() < 0.2 else rng.randrange(1, 6), 1),
               (tile(tx, ty) + 0x2, target_pile, 2),
               (b + 0x21, buildup, 1)]
    for field in (0x16, 0x17):
        bits = rng.choice([0, 1 << rng.randrange(8), rng.getrandbits(8)])
        fields.append((b + field, bits, 1))
    fields += pile_on_tile(rng, target_pile)
    for k in range(3):
        pile = rng.randrange(1, 0x1000)
        fields += [(b + 0x10 + 2 * k, pile, 2),
                   (PILES + pile * PILE_BYTES, rng.choice([slot, good, rng.randrange(36)]), 1)]
    if 0x4c <= job <= 0x4f:
        p = PILES + pile_word * PILE_BYTES
        fields += pile_on_tile(rng, pile_word)
        fields += [(p + 0x4, 0xfc if rng.random() < 0.25 else rng.choice(PILE_KINDS), 1),
                   (p + 0x1, 0 if rng.random() < 0.5 else rng.randrange(5), 1),
                   (p + 0x3, 1 if rng.random() < 0.5 else rng.randrange(5), 1)]
    return fields


def take_good_state(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    amount = rng.choice([1, 1, 2, 0, rng.randrange(256)])
    return pile_on_tile(rng, args[0]) + spiral_offsets(19) + [
        (record + 0x1, amount, 1), (record + 0x2, rng.randrange(4), 1),
        (record + 0x3, 0 if rng.random() < 0.6 else rng.randrange(1, 4), 1),
        (record + 0x4, rng.choice([0, 0, 0xfe, 0xfe, 0xff, 7]), 1)]


SPECS += [
    Spec("CGameData_ResetSettlerToIdle", 0x0050cf50, "thiscall", [between(1, 0x3000)],
         presets=idle_state, max_cases=1500),
    Spec("CGameData_TakeGoodFromPile", 0x00505820, "thiscall", [between(1, 0xfff)], presets=take_good_state,
         max_cases=1500),
]


def steal_state(rng, pool, args):
    """The pile (kind from the switch's cases), amount/leaving around the
    'all promised' edge, and settlers 1..count, some fetching from its tile
    (fetch jobs, 0x133/0x134) with small building/pile words."""
    pile = args[0]
    record = PILES + pile * PILE_BYTES
    kind = rng.choice([0, 5, 0x32, 0x33, 0xfc, 0xfc, 0xfd, 0xfe, 0xfe, 0xff, 0xff, 0x80])
    amount = 0 if rng.random() < 0.1 else rng.randrange(1, 5)
    leaving = (amount + rng.randrange(2)) if rng.random() < 0.5 else (rng.randrange(5) | (rng.randrange(3) << 4))
    x, y = rng.randrange(10, 370), rng.randrange(10, 370)
    count = rng.randrange(30)
    fields = [(MAP_SIZE, 384, 4), (BUILDING_COUNT, rng.randrange(20), 4), (SETTLER_HIGH_WATER, count, 4),
              (record + 0x4, kind, 1), (record + 0x1, amount, 1), (record + 0x2, leaving & 0xFF, 1),
              (record + 0x6, x, 2), (record + 0x8, y, 2), (tile_area_offset(x, y), rng.randrange(AREA_LIMIT), 2),
              (tile(x, y) + 0x2, pile, 2), (record + 0xa, rng.randrange(0x1000), 2),
              (record + 0xc, rng.randrange(0x1000), 2)] + spiral_offsets(19)
    for s in range(1, count + 1):
        r = SETTLERS + s * SETTLER_BYTES
        here = rng.random() < 0.3
        fields += [(r + 0x0, 0xff if rng.random() < 0.15 else rng.randrange(4), 1),
                   (r + 0xe, rng.choice([0xa, 0xb, 0xa9, 0x68, 0x69, 0x133, 0x134, 5]), 2),
                   (r + 0x24, x if here else rng.randrange(384), 2), (r + 0x26, y if here else rng.randrange(384), 2),
                   (r + 0x17, rng.randrange(2), 1), (r + 0x15, 0, 1), (r + 0x22, rng.randrange(1, 20), 2),
                   (r + 0x4, rng.randrange(10, 370), 2), (r + 0x6, rng.randrange(10, 370), 2)]
    return fields


SPECS += [
    Spec("CGameData_StealGoodFromPile", 0x00505c20, "thiscall", [between(1, 0xfff)], eax_mask=0xFF,
         presets=steal_state, max_cases=1500),
]


# Package B, batch 4 (miner_jobs.cpp: MinerJobFood, MinerJobDig)
MINES = [7, 8, 9, 0x25, 0x1d, 0x38]
TALLIES = 0x007acfd4
CAMPAIGN_MISSION = 0x007dfdb0


def miner_common(rng, settler, building, art):
    record = SETTLERS + settler * SETTLER_BYTES
    b = BUILDINGS + building * BUILDING_BYTES
    owner = rng.randrange(4)
    bx, by = rng.randrange(10, 370), rng.randrange(10, 370)
    fields = [(MAP_SIZE, 384, 4), (BUILDING_COUNT, rng.randrange(20), 4),
              (record + 0x22, building, 2), (record + 0x0, owner, 1), (record + 0x4, bx, 2),
              (record + 0x6, by + 1, 2), (b + 0x6, art, 1), (b + 0x2, bx, 2), (b + 0x4, by, 2),
              (PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES, rng.choice([1, 2, 0]), 4),
              (PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES + 4, rng.randrange(4), 4)]
    return fields, record, b, bx, by


def miner_food_state(rng, pool, args):
    building = rng.randrange(1, 0x100)
    fields, record, b, bx, by = miner_common(rng, args[0], building, rng.choice(MINES + [0x30]))
    fields += spiral_offsets(19)
    fields.append((record + 0x17, 0 if rng.random() < 0.6 else rng.randrange(1, 20), 1))
    for k in range(3):
        pile = 0 if rng.random() < 0.15 else rng.randrange(1, 0x1000)
        fields.append((b + 0x10 + 2 * k, pile, 2))
        if pile:
            fields += pile_on_tile(rng, pile)
            fields += [(PILES + pile * PILE_BYTES + 0x1, 0 if rng.random() < 0.5 else rng.randrange(1, 4), 1),
                       (PILES + pile * PILE_BYTES + 0x2, 0, 1),
                       (PILES + pile * PILE_BYTES + 0x4, rng.choice([0, 0xfe]), 1)]
    out = rng.randrange(1, 0x1000)
    fields += [(b + 0xe, out, 2), (b + 0x18, rng.getrandbits(8), 1),
               (PILES + out * PILE_BYTES + 0x1, rng.randrange(5, 10), 1)]
    return fields


def miner_dig_state(rng, pool, args):
    building = rng.randrange(1, 0x100)
    art = rng.choice(MINES)
    fields, record, b, bx, by = miner_common(rng, args[0], building, art)
    for k in range(64):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, (k % 9 - 4) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, (k // 9 - 3) & 0xFFFFFFFF, 4)]
    for dx in range(-4, 5):
        for dy in range(-3, 5):
            fields.append((tile(bx + dx, by + dy) + 0x11,
                           rng.choice([0x10, 0x20, 0x30, 0x40, 0x50]) | rng.randrange(16), 1))
    fields += [(b + 0x30, 0 if rng.random() < 0.5 else rng.getrandbits(16), 2), (b + 0x1a, rng.randrange(0x22), 1)]
    return fields + settler_animation_gfx(rng, args[0])


def settler_animation_gfx(rng, settler):
    """Every animation kind the mines use made a valid sprite for the settler's race."""
    race = rng.randrange(4)
    fields = [(SETTLERS + settler * SETTLER_BYTES + 2, race, 1)]
    for kind in (0x5e, 0x5f, 0x64, 0x65, 0x61, 0x62, 0xd8, 0xd9, 0xdb, 0xdc):
        fields += gfx_sprite(animation_table()[kind * 4 + race], rng)
    return fields


def miner_globals(rng, pool, args):
    return [(CAMPAIGN_MISSION, rng.choice([0, 0, 3, 12]), 4), (0x007a7af0 + 0x189, rng.randrange(2), 1),
            (0x007a8010, WORLD_BASE + GFX_OBJECT, 4)]


SPECS += [
    Spec("CGameData_MinerJobFood", 0x004f9b80, "thiscall", [between(1, 0x3000)], eax_mask=0,
         presets=miner_food_state, globals=miner_globals, max_cases=1500),
    Spec("CGameData_MinerJobDig", 0x004fa220, "thiscall", [between(1, 0x3000)], eax_mask=0,
         presets=miner_dig_state, globals=miner_globals, max_cases=1500),
]


# Package C, batches 6-7: RemoveBuildingGoods 0x4de430 and the destruction
# cycle (CrushBuilding 0x4d9250, DestroyBuildingAt 0x4d97b0,
# ReleaseSiteWorkers 0x50e3b0, RecalculateTerritory 0x519a90). Each spec
# probes the other cycle functions (the recursion is tested in-process,
# CGameData_DestructionCycle_MatchesOriginal), package B's callees and the
# verified callees that flood regions or need a consistent map
# (UnblockTile, SetOwnerOfFreeTile, ClearTileOwner, the Drop*Pile functions
# and the MapEffects calls, all with own specs); calls are compared.
RESET = {0x0050cf50: (1, "thiscall", "probe")}
_LAYOUTS: dict = {}


def layout_dwords(race, type_):
    """The 600 dwords of the building layout (S3.EXE 0x6aa170)."""
    key = (race, type_)
    if key not in _LAYOUTS:
        import os
        import struct

        import pefile
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "Settlers3Ultimate", "S3.EXE")
        pe = pefile.PE(path, fast_load=True)
        data = pe.get_data(0x6aa170 + (type_ + race * 57) * 2400 - 0x400000, 2400)
        _LAYOUTS[key] = struct.unpack("<600I", data)
    return _LAYOUTS[key]


def footprint(race, type_):
    """(dx, dy) of the layout's footprint list (after 0x80000000 up to 0x80000001)."""
    d = layout_dwords(race, type_)
    k = d.index(0x80000000) + 1
    out = []
    while d[k] != 0x80000001:
        dx, dy = d[k], d[k + 1]
        out.append((dx - (1 << 32) if dx & 0x80000000 else dx, dy - (1 << 32) if dy & 0x80000000 else dy))
        k += 2
    return out


def worker_settlers(rng, number, x, y, garrison=True):
    """Settlers 1..30 bound to the building (site and garrison jobs) and a
    chain of up to 4 garrison soldiers (next +0x10); returns (fields, head)."""
    fields = [(SETTLER_COUNT, 30, 4)]
    jobs = (0x68, 0x69, 3, 4, 5, 6, 7, 8, 9, 0xE, 0xF, 0x11, 0x12, 0x10, 0x6A, 0x8B, 0x8C, 1, 0x37, 0xA, 0xB, 0xA9,
            0xAA, 0xC, 0xD, 0x50)
    for n in range(1, 31):
        s = SETTLERS + n * 0x40
        fields += [(s, 0xFF if rng.random() < 0.12 else rng.randrange(4), 1),
                   (s + 1, rng.randrange(3) if rng.random() < 0.5 else rng.randrange(0x30), 1),
                   (s + 0xE, rng.choice(jobs), 2), (s + 0x18, rng.randrange(4), 1),
                   (s + 0x22, number if rng.random() < 0.65 else rng.randrange(60), 2),
                   (s + 0x14, rng.randrange(256) & 0xBF, 1), (s + 0x10, 0, 2),
                   (s + 4, x + rng.randrange(-5, 6), 2), (s + 6, y + rng.randrange(-5, 6), 2)]
    head = 0
    if garrison:
        members = rng.sample(range(1, 31), rng.randrange(5))
        for k, n in enumerate(members):
            s = SETTLERS + n * 0x40
            fields += [(s + 0x10, members[k + 1] if k + 1 < len(members) else 0, 2), (s + 0x22, number, 2),
                       (s + 0x14, rng.randrange(256), 1)]
        head = members[0] if members else 0
    return fields, head


def building_record(rng, number, type_, x, y, buildup=None):
    b = BUILDINGS + number * BUILDING_BYTES
    if buildup is None:
        buildup = 0 if rng.random() < 0.6 else rng.randint(1, 10)
    return [(b, rng.randrange(4), 1), (b + 6, type_, 1), (b + 7, rng.randrange(4), 1), (b + 0x21, buildup, 1),
            (b + 2, x, 2), (b + 4, y, 2), (b + 0xA, x + 1, 2), (b + 0xC, y + 1, 2), (b + 0x16, rng.randrange(256), 1),
            (b + 0x17, rng.randrange(256), 1), (b + 0x1F, rng.randrange(256), 1),
            (b + 0x20, 0xFF if rng.random() < 0.25 else rng.randrange(256), 1),
            (b + 0x22, 0xFFFF if rng.random() < 0.3 else rng.randrange(30), 2)]


def remove_goods_world(rng, pool, args):
    number = args[0]
    type_ = rng.choice([1, 0xC, 0xD, 0x35, 0x21, 0x22, 2, 3, 0x1D, 0x38, 0x34, 0xF, 0x1A]) if rng.random() < 0.7 \
        else rng.randint(1, 0x38)
    x, y = rng.randint(30, 350), rng.randint(30, 350)
    b = BUILDINGS + number * BUILDING_BYTES
    fields = building_record(rng, number, type_, x, y)
    race = fields[2][1]
    materials = layout_dwords(race, type_)[1]
    fields += [(b + 0x19, rng.randrange(((materials >> 8) & 0xFF) + 1), 1),
               (b + 0x1A, rng.randrange((materials & 0xFF) + 1), 1), (b + 0xE, rng.getrandbits(16), 2)]
    for k in range(3):
        pile = 0 if rng.random() < 0.25 else rng.randint(1, 150)
        fields.append((b + 0x10 + 2 * k, pile, 2))
        if pile:
            p = PILES + pile * PILE_BYTES
            fields += [(p + 6, rng.randint(20, 360), 2), (p + 8, rng.randint(20, 360), 2),
                       (p + 1, 0 if rng.random() < 0.3 else rng.randrange(9), 1), (p + 3, rng.randrange(9), 1),
                       (p, rng.randint(1, 35), 1)]
    area = rng.randint(1, 50)
    fields += [(tile(x + 1, y + 1) + 0x14, area, 2)]
    fields += [(AREAS + area * AREA_BYTES + 0x52 + 2 * g, 0, 2) for g in range(36)]
    f, _ = worker_settlers(rng, number, x, y, garrison=False)
    return fields + f


def release_workers_world(rng, pool, args):
    number = args[0]
    type_ = rng.choice([0xF, 0x10, 0x12, 0x13, 2, 3, 0x1D, 0x21, 1]) if rng.random() < 0.75 else rng.randrange(0x39)
    x, y = rng.randint(30, 330), rng.randint(30, 330)
    fields = building_record(rng, number, type_, x, y)
    f, head = worker_settlers(rng, number, x, y, garrison=rng.random() < 0.8)
    fields += f + [(BUILDINGS + number * BUILDING_BYTES + 8, head, 2)]
    return fields


CRUSH_PROTECTED = ((0x10A, 0x215), (0x114, 0x20C), (0x125, 0x200), (0x138, 0x1FF), (0x143, 0x1FE), (0x156, 0x203),
                   (0x168, 0x210), (0x179, 0x214), (0x194, 0x22B), (0x174, 0x220), (0x16D, 0x24A), (0x15C, 0x252),
                   (0x146, 0x254), (0x131, 0x24F), (0x129, 0x242), (0x14B, 0x23A), (0x1AD, 0x21E))


def crush_world(rng, pool, args):
    number = args[0]
    type_ = rng.choice([0xF, 0x10, 0x12, 0x20, 0x21, 0x1F, 0x2F, 0x30, 0x32, 3]) if rng.random() < 0.75 \
        else rng.randrange(0x39)
    x, y = rng.randint(30, 350), rng.randint(30, 350)
    if rng.random() < 0.15:
        x, y = rng.choice(CRUSH_PROTECTED)
    b = BUILDINGS + number * BUILDING_BYTES
    fields = building_record(rng, number, type_, x, y)
    fields += [(MAP_SIZE, 384, 4), (b, 0xFF if rng.random() < 0.04 else rng.randrange(4), 1),
               (b + 8, 0 if rng.random() < 0.25 else rng.randint(1, 50), 2),
               (0x32870, 0, 4), (0x32874, 0, 4), (0x366f8, 0, 4), (0x366fc, 0, 4)]
    if type_ in (0xF, 0x10, 0x12):
        fields.append((b + 0x22, rng.randint(1, 50), 2))
    for p in range(4):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xC, rng.randrange(3), 4))
    fields += site_offset_table(rng, rings=16)
    for dx in range(-16, 17):
        for dy in range(-16, 17):
            fields.append((tile(x + dx, y + dy), rng.randint(1, 60) if rng.random() < 0.03 else 0, 2))
    for n in range(1, 61):
        s = SETTLERS + n * 0x40
        fields += [(s, rng.randrange(4), 1), (s + 1, rng.randrange(0x30), 1),
                   (s + 4, x + rng.randrange(-5, 6), 2), (s + 6, y + rng.randrange(-5, 6), 2)]
    return fields


def crush_globals(rng, pool, args):
    return [(0x007dfdb0, 70 if rng.random() < 0.3 else 3, 4), (0x007dfd48, rng.getrandbits(16), 4)]


def destroy_at_world(rng, pool, args):
    """One to three buildings around the target, their footprints marked;
    the first one placed so that one of its footprint tiles is the target."""
    x, y = args
    fields = [(MAP_SIZE, 384, 4)] + site_offset_table(rng, rings=12)
    for dx in range(-14, 15):
        for dy in range(-14, 15):
            fields.append((tile(x + dx, y + dy) + 0xA, rng.randrange(256) & 0xDF, 1))
    for k in range(rng.randint(1, 3)):
        number = rng.randint(1, 50)
        type_, race = rng.randint(1, 56), rng.randrange(4)
        tiles = [(0, 0)] + footprint(race, type_)
        if k == 0:
            dx, dy = rng.choice(tiles)
            bx, by = x - dx, y - dy
        else:
            bx, by = x + rng.randrange(-3, 4), y + rng.randrange(-3, 4)
        b = BUILDINGS + number * BUILDING_BYTES
        fields += [(b + 6, type_, 1), (b + 7, race, 1), (b + 2, bx, 2), (b + 4, by, 2)]
        for dx, dy in tiles:
            t = tile(bx + dx, by + dy)
            fields += [(t + 0xA, 0x28, 1), (t + 2, number, 2)]
    if rng.random() < 0.5:  # the target tile itself unmarked: the search goes on
        fields.append((tile(x, y) + 0xA, 0, 1))
    return fields


def territory_world(rng, pool, args):
    """A small map (40..90) with owners in two halves, some free tiles, and
    up to six buildings (mostly garrisoned military) on it."""
    x, y = args
    n = rng.randint(40, 90)
    fields = [(MAP_SIZE, n, 4)] + site_offset_table(rng, rings=41)
    split = rng.randint(0, n)
    for tx in range(-2, n + 2):
        for ty in range(-2, n + 2):
            t = tile(tx, ty)
            owner = 0xFF if (tx + ty) % 7 == 0 else (0 if tx < split else 1 + (ty & 1))
            fields += [(t + 8, owner, 1), (t + 0xA, rng.randrange(256) & 0x5F, 1), (t + 2, 0, 2)]
    for k in range(rng.randrange(7)):
        number = k + 1
        bx, by = rng.randrange(n), rng.randrange(n)
        b = BUILDINGS + number * BUILDING_BYTES
        fields += [(b, rng.randrange(4), 1), (b + 6, rng.choice([0xF, 0x10, 0x12, 0x12, 3]), 1),
                   (b + 0x21, 0 if rng.random() < 0.8 else 1, 1), (b + 8, rng.randint(1, 99) if rng.random() < 0.85 else 0, 2),
                   (tile(bx, by) + 0xA, 0x28, 1), (tile(bx, by) + 2, number, 2)]
    return fields


def territory_args(rng, pool):
    return rng.randrange(-5, 95) & 0xFFFFFFFF


SPECS += [
    Spec("CGameData_RemoveBuildingGoods", 0x004de430, "thiscall", [between(1, 50)], eax_mask=0,
         presets=remove_goods_world, max_cases=800,
         callees={**RESET, 0x005046b0: (4, "thiscall", "probe"), 0x00504a00: (1, "thiscall", "probe"),
                  0x00504b70: (1, "thiscall", "probe"), 0x00504e20: (1, "thiscall", "probe")}),
    Spec("CGameData_ReleaseSiteWorkers", 0x0050e3b0, "thiscall", [between(1, 50)], eax_mask=0,
         presets=release_workers_world, max_cases=800, callees={**RESET, 0x00519a90: (2, "thiscall", "probe")}),
    Spec("CGameData_CrushBuilding", 0x004d9250, "thiscall", [between(1, 50)], presets=crush_world,
         globals=crush_globals, max_cases=600,
         callees={0x0050e3b0: (1, "thiscall", "probe"), 0x004de430: (1, "thiscall", "probe"),
                  0x0051aae0: (1, "thiscall", "probe"), 0x004d9940: (1, "thiscall", "probe"),
                  0x004dbb90: (1, "thiscall", "probe"), 0x004dda10: (1, "thiscall", "probe"),
                  0x00544f20: (5, "thiscall", "probe"), 0x00545c80: (4, "thiscall", "probe")}),
    # 0x4d9909: the DebugLog after 9900 offset entries without finding the
    # building (an error path; it needs the whole offset table preset and a
    # map without the building).
    Spec("CGameData_DestroyBuildingAt", 0x004d97b0, "thiscall", [between(30, 350), between(30, 350)],
         presets=destroy_at_world, max_cases=500, callees={0x004d9250: (1, "thiscall", "probe")},
         exempt_blocks=(0x4d9909,)),
    Spec("CGameData_RecalculateTerritory", 0x00519a90, "thiscall", [territory_args, territory_args],
         presets=territory_world, max_cases=25, max_instructions=400_000_000,
         callees={0x004d97b0: (2, "thiscall", "probe"), 0x0051b5d0: (3, "thiscall", "probe"),
                  0x0051b640: (2, "thiscall", "probe")}),
]


# Package A, batch 3: whole paths (pathing.cpp): TraceLine, FollowRightWall,
# PlanLine, FindPath on a window of free tiles (regions 0) with obstacles
# (regions 1); the lazily random tiles around it almost never match the
# regions, so they act as a wall.
ROUTE_WINDOW = 12
SPLIT = (0, 1, 1, -1, -1, 1, 1, 0, -1, 0, 0, 1, 0, -1, -1, 1, 1, -1, -1, 0, 1, 0, 0, -1)  # 0x6586ac
AXES = (1, 1, 1, 0, 0, 1, 1, 1, -1, 0, 0, 1, -1, -1, -1, 0, 0, -1, -1, -1, 1, 0, 0, -1)   # 0x65870c
ROUTE_MODES = (1, 2, 8, 0x18)


def route_window(rng, x, y):
    """Fields of the window around (x, y) and the set of obstacle tiles."""
    fields, blocked = [], set()
    r = ROUTE_WINDOW
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            t = tile(x + dx, y + dy)
            other = rng.randint(1, 31999) if rng.random() < 0.1 else 0
            fields += [(t + 0x10, 0, 1), (t + 0x14, 0, 2), (t + 0x16, 0, 2), (t + 0x0, other, 2),
                       (t + 0x6, rng.choice((0x10, 0x12, 0x13, 0x18, 0x1C, 0x16, 0x05, 0x20)), 1)]
            if other:
                record = SETTLERS + other * SETTLER_BYTES
                fields += [(record + 0xE, rng.choice(PATH_JOBS), 2), (record + 0x2C, rng.randrange(256), 1),
                           (record + 0x2E, rng.randrange(6), 1)]
    for _ in range(rng.randrange(6)):
        ox, oy = x + rng.randint(-r + 3, r - 3), y + rng.randint(-r + 3, r - 3)
        cells = []
        if rng.random() < 0.5:
            d, side, length = rng.randrange(6), rng.randrange(6), rng.randint(3, 12)
            thick = rng.random() < 0.5
            for i in range(-length // 2, length // 2 + 1):
                cx, cy = ox + i * DIR_X[d], oy + i * DIR_Y[d]
                cells.append((cx, cy))
                if thick:
                    cells.append((cx + DIR_X[side], cy + DIR_Y[side]))
        else:
            rad = rng.randint(1, 3)
            cells = [(ox + dx, oy + dy) for dx in range(-rad, rad + 1) for dy in range(-rad, rad + 1)
                     if abs(dx) + abs(dy) <= rad + 1]
        for cx, cy in cells:
            if abs(cx - x) <= r and abs(cy - y) <= r:
                t = tile(cx, cy)
                fields += [(t + 0x10, 1, 1), (t + 0x14, 1, 2), (t + 0x16, 1, 2)]
                blocked.add((cx, cy))
    return fields, blocked


def direction_of_delta(dx, dy):
    if dx > 0 and dy >= 0:
        return 1 if dx <= dy else 0
    if dx <= 0 and dy > 0:
        return 2
    if dx < 0 and dy <= 0:
        return 4 if dx >= dy else 3
    return 5


def route_case(rng, n, state=True, line=True):
    """Settler n at (x, y) in a window, its target in the window (free in
    four of five cases), a realistic path mode (4 only ever comes from
    WalkLine in the same FindPath round), and (state) the path state as
    FindPath and PlanLine set it up for the line from the target."""
    x, y = rng.randint(20, 360), rng.randint(20, 360)
    fields, blocked = route_window(rng, x, y)
    tx, ty = x + rng.randint(-10, 10), y + rng.randint(-10, 10)
    if (tx, ty) == (x, y):
        tx += 1
    wx = x if rng.random() < 0.5 else x + rng.randint(-6, 6)
    wy = y if rng.random() < 0.5 else y + rng.randint(-6, 6)
    mode = rng.choice(ROUTE_MODES) | (rng.randrange(4) << 6) | (0x20 if rng.random() < 0.25 else 0)
    record = SETTLERS + n * SETTLER_BYTES
    here = tile(x, y)
    fields += [(MAP_SIZE, 384, 4), (here + 0x10, 0, 1), (here + 0x14, 0, 2), (here + 0x16, 0, 2), (here, n, 2),
               (record + 0x1, rng.randrange(48), 1), (record + 0x4, x, 2), (record + 0x6, y, 2),
               (record + 0x8, rng.randrange(6) if rng.random() < 0.9 else rng.getrandbits(32), 4),
               (record + 0xE, 0xAB if rng.random() < 0.1 else 2, 2), (record + 0x24, tx, 2), (record + 0x26, ty, 2),
               (record + 0x28, wx, 2), (record + 0x2A, wy, 2), (record + 0x2C, mode, 1),
               (record + 0x2D, rng.randrange(5), 1), (record + 0x2E, rng.randrange(6), 1)]
    if rng.random() < 0.8:
        t = tile(tx, ty)
        fields += [(t + 0x10, 0, 1), (t + 0x14, 0, 2), (t + 0x16, 0, 2)]
    shape = rng.random()
    if shape < 0.15:
        # The settler walled in (all six neighbours blocked): no step in
        # three rounds, FindPath's fallback direction.
        for k in range(6):
            t = tile(x + DIR_X[k], y + DIR_Y[k])
            fields += [(t + 0x10, 1, 1), (t + 0x14, 1, 2), (t + 0x16, 1, 2)]
    elif shape < 0.3:
        # The settler surrounded by working settlers (stationary jobs): the
        # line is free when target_free is 0, but no step is possible, so
        # FindPath runs three rounds and takes its fallback direction.
        for k in range(6):
            other = rng.randint(1, 31999)
            fields += [(tile(x + DIR_X[k], y + DIR_Y[k]), other, 2),
                       (SETTLERS + other * SETTLER_BYTES + 0xE, rng.choice((4, 5, 0x100)), 2)]
    elif shape < 0.5:
        # A ring of radius 2..4 around the settler with a gap: PlanLine may
        # need many rounds (intermediate targets) or give up.
        rad, gap = rng.randint(2, 4), rng.randrange(6)
        for k in range(6):
            for j in range(rad):
                if k == gap and j == rad // 2:
                    continue
                cx = x + rad * DIR_X[k] + j * DIR_X[(k + 2) % 6]
                cy = y + rad * DIR_Y[k] + j * DIR_Y[(k + 2) % 6]
                t = tile(cx, cy)
                fields += [(t + 0x10, 1, 1), (t + 0x14, 1, 2), (t + 0x16, 1, 2)]
    if state:
        fields += [(PATH_STATE + 0x0, n, 4), (PATH_STATE + 0x4, x, 4), (PATH_STATE + 0x8, y, 4),
                   (PATH_STATE + 0x54, 0, 4), (PATH_STATE + 0x58, 0, 4), (PATH_STATE + 0x5C, 0, 4),
                   (PATH_STATE + 0x60, rng.choice([1, 2, 3]), 4),
                   (PATH_STATE + 0x48, 1000 if rng.random() < 0.2 else rng.randrange(6), 4),
                   (PATH_STATE + 0x4C, rng.randint(1, 2), 4), (PATH_STATE + 0x94, rng.randrange(2), 4)]
    if line:
        dx, dy = x - tx, y - ty
        d = direction_of_delta(dx, dy)
        a = SPLIT[d * 4 + 1] * dy + SPLIT[d * 4] * dx
        b = SPLIT[d * 4 + 3] * dy + SPLIT[d * 4 + 2] * dx
        ax_x, bx_x, ax_y, bx_y = AXES[d * 4:d * 4 + 4]
        if a > b:
            f = int((b << 16) / a)
            major_a, major_b, minor_a, minor_b = 0x10000, 0, 0, f
        else:
            f = int((a << 16) / b)
            major_a, major_b, minor_a, minor_b = 0, 0x10000, f, 0
        values = {0x0C: dx, 0x10: dy, 0x14: abs(dx), 0x18: abs(dy), 0x1C: a, 0x20: b, 0x34: ax_x, 0x38: bx_x,
                  0x3C: ax_y, 0x40: bx_y, 0x44: d, 0x64: tx, 0x68: ty,
                  0x6C: ax_x * major_a + bx_x * major_b, 0x70: ax_y * major_a + bx_y * major_b,
                  0x74: ax_x * minor_a + bx_x * minor_b, 0x78: ax_y * minor_a + bx_y * minor_b,
                  0x7C: (tx << 16) + 0x8000, 0x80: (ty << 16) + 0x8000, 0x94: 0}
        fields += [(PATH_STATE + k, v & 0xFFFFFFFF, 4) for k, v in values.items()]
        fields.append((PATH_STATE + 0x98, rng.randrange(2), 1))
    return fields, (x, y, tx, ty, blocked)


def route_trace(rng, pool, args):
    return route_case(rng, rng.randint(1, 31999))[0]


def route_wall(rng, pool, args):
    """FollowRightWall after a blocked trace: a hit point (a free tile next to
    an obstacle, or a random one), the blocked direction and a trace point
    behind the obstacle (or anywhere)."""
    fields, (x, y, tx, ty, blocked) = route_case(rng, settler_number(args[0]))
    r = ROUTE_WINDOW
    hx, hy = x + rng.randint(-r + 1, r - 1), y + rng.randint(-r + 1, r - 1)
    d = rng.randrange(6)
    if blocked and rng.random() < 0.8:
        bx, by = rng.choice(sorted(blocked))
        hx, hy = bx - DIR_X[d], by - DIR_Y[d]
        px, py = bx + DIR_X[d], by + DIR_Y[d]
        while (px, py) in blocked:
            px, py = px + DIR_X[d], py + DIR_Y[d]
    else:
        px, py = x + rng.randint(-r, r), y + rng.randint(-r, r)
    if rng.random() < 0.3:
        # A trace point next to the hit point: reached in the first round.
        px, py = hx + rng.randint(-2, 2), hy + rng.randint(-2, 2)
    if rng.random() < 0.6:
        # No retries in the path mode: the first round runs (when the
        # trace point lies behind the hit point).
        record = SETTLERS + settler_number(args[0]) * SETTLER_BYTES
        fields.append((record + 0x2C, rng.choice(ROUTE_MODES) | (0x20 if rng.random() < 0.25 else 0), 1))
    if rng.random() < 0.1:
        # An enclosed hit point (every neighbour blocked): "no direction".
        for k in range(6):
            t = tile(hx + DIR_X[k], hy + DIR_Y[k])
            fields += [(t + 0x10, 1, 1), (t + 0x14, 1, 2), (t + 0x16, 1, 2)]
    t = tile(hx, hy)
    fields += [(t + 0x10, 0, 1), (t + 0x14, 0, 2), (t + 0x16, 0, 2)]
    if rng.random() < 0.2:
        # A pocket: the hit point is a blocked tile (the trace's start, the
        # target, when the first step was blocked) with one or two exits,
        # each sometimes a dead end. Reaches a walker that is enclosed after
        # its first step. target_free 0: settlers don't block.
        def block(bx, by):
            tb = tile(bx, by)
            return [(tb + 0x10, 1, 1), (tb + 0x14, 1, 2), (tb + 0x16, 1, 2)]

        def free(bx, by):
            tb = tile(bx, by)
            return [(tb + 0x10, 0, 1), (tb + 0x14, 0, 2), (tb + 0x16, 0, 2)]

        exits = {rng.randrange(6), rng.randrange(6)}
        fields += block(hx, hy) + [(PATH_STATE + 0x98, 0, 1)]
        for k in range(6):
            if k not in exits:
                fields += block(hx + DIR_X[k], hy + DIR_Y[k])
        for k in exits:
            ex, ey = hx + DIR_X[k], hy + DIR_Y[k]
            if rng.random() < 0.6:
                for m in range(6):
                    if (ex + DIR_X[m], ey + DIR_Y[m]) != (hx, hy):
                        fields += block(ex + DIR_X[m], ey + DIR_Y[m])
            fields += free(ex, ey)
    target = None
    if rng.random() < 0.3:
        # The "no angle" logs compare the target with the walkers' best
        # point: a tile near the hit point.
        k, j = rng.randrange(6), rng.randint(1, 3)
        target = (hx + j * DIR_X[k], hy + j * DIR_Y[k])
    shape = rng.random()
    if shape < 0.12:
        # A straight corridor hit point -> P -> trace point: the right-wall
        # walker's best point after one step is P, and the target is P when
        # it reaches the trace point (the "no angle" comparison, 0x4a453c).
        k = rng.randrange(6)
        hx, hy = x + rng.choice([-9, -8, 8, 9]), y + rng.choice([-9, -8, 8, 9])
        p1 = (hx + DIR_X[k], hy + DIR_Y[k])
        px, py = p1[0] + DIR_X[k], p1[1] + DIR_Y[k]
        open_tiles = {(hx, hy), p1, (px, py)}
        walls = {(cx + DIR_X[m], cy + DIR_Y[m]) for cx, cy in ((hx, hy), p1) for m in range(6)} - open_tiles
        target = p1
    elif shape < 0.24:
        # The hit point joins two dead-end corridors of different lengths:
        # the walkers go into different ones, and when the left one comes
        # back first (0x81) the right one comes back later ("Rightwall, No
        # way", 0x4a44c9; target_free 0).
        hx, hy = x + rng.choice([-9, -8, 8, 9]), y + rng.choice([-9, -8, 8, 9])
        k1 = rng.randrange(6)
        k2 = (k1 + rng.choice([2, 3, 4])) % 6
        l1, l2 = rng.sample(range(1, 5), 2)
        open_tiles = {(hx, hy)} | {(hx + i * DIR_X[k1], hy + i * DIR_Y[k1]) for i in range(1, l1 + 1)}
        open_tiles |= {(hx + i * DIR_X[k2], hy + i * DIR_Y[k2]) for i in range(1, l2 + 1)}
        walls = {(cx + DIR_X[m], cy + DIR_Y[m]) for cx, cy in open_tiles for m in range(6)} - open_tiles
        px, py = x, y
        fields.append((PATH_STATE + 0x98, 0, 1))
    else:
        open_tiles = walls = ()
    for cx, cy in walls:
        tw = tile(cx, cy)
        fields += [(tw + 0x10, 1, 1), (tw + 0x14, 1, 2), (tw + 0x16, 1, 2)]
    for cx, cy in open_tiles:
        to = tile(cx, cy)
        fields += [(to + 0x10, 0, 1), (to + 0x14, 0, 2), (to + 0x16, 0, 2), (to + 0x0, 0, 2)]
    fields += [(PATH_STATE + 0x84, (hx << 16) + 0x8000, 4), (PATH_STATE + 0x88, (hy << 16) + 0x8000, 4),
               (PATH_STATE + 0x8C, d, 4), (PATH_STATE + 0x7C, (px << 16) + 0x8000, 4),
               (PATH_STATE + 0x80, (py << 16) + 0x8000, 4)]
    if target is not None:
        fields += [(PATH_STATE + 0x64, target[0], 4), (PATH_STATE + 0x68, target[1], 4)]
    return fields


def route_plan(rng, pool, args):
    return route_case(rng, settler_number(args[0]), line=False)[0]


def route_find(rng, pool, args):
    return route_case(rng, args[0], state=False, line=False)[0]


SPECS += [
    Spec("Pathing_TraceLine", 0x004a3e30, "thiscall", [scratch(0)], presets=route_trace, max_cases=1000),
    # Exempt: 0x4a4546/0x4a4550 and 0x4a476d/0x4a4777/0x4a4781, the "no
    # angle" DebugLogs: they need the target to equal the walker's best
    # point just when the other walker has failed (the preset would have
    # to replay both walks); they only log (silent in the release build)
    # and the comparisons in front of them are covered; the in-process
    # route test compares the DebugLog calls. 0x4a4422/0x4a4438 (right-wall
    # walker enclosed while the left one already failed, 0x81): dead. A
    # walker can only be enclosed at the start tile or right after leaving
    # a blocked start tile (its previous tile is free otherwise), i.e. in
    # the first two rounds, and the left walker can only fail in round 1
    # when enclosed at the same start tile (then the right one failed
    # first) and not by returning to a blocked start tile.
    Spec("Pathing_FollowRightWall", 0x004a4050, "thiscall", [walk_settler, scratch(0)], presets=route_wall,
         max_cases=600,
         exempt_blocks=(0x4a4422, 0x4a4438, 0x4a4546, 0x4a4550, 0x4a476d, 0x4a4777, 0x4a4781),
         max_instructions=300_000_000),  # walks of up to 10000 rounds in the debug build
    Spec("Pathing_PlanLine", 0x004a3ab0, "thiscall", [walk_settler, scratch(0)], presets=route_plan, max_cases=300,
         max_instructions=300_000_000),
    # 0x4a229c/0x4a22a6 (fallback while standing on the waypoint): dead.
    # Standing on the waypoint sets mode 1 at the round's start, and PlanLine
    # then sets the waypoint to the target or to a walker's best point,
    # never the settler's own tile (a target there asserts earlier).
    Spec("Pathing_FindPath", 0x004a1ee0, "thiscall", [between(1, 31999)], presets=route_find, max_cases=300,
         max_instructions=300_000_000, exempt_blocks=(0x4a229c, 0x4a22a6)),
]


# --- package D batch 1: fog of war, map effect expiry, traffic, spell cost --

SIGHT_MAP = 0x2d2d0
LOCAL_PLAYER = 0x2d2d4
REVEAL_COUNT = 0x14d28a4
REVEAL_POINTS = 0x14d28a8


def ring_table(rng, top: int):
    """The ring table at 0x110a588 with every distance 0..top (the sight loops
    stop at the first entry whose distance equals the radius), 1..3 offsets
    per distance."""
    fields, n = [], 0
    for d in range(top + 1):
        for _ in range(1 if d == 0 else rng.randint(1, 3)):
            dx, dy = (0, 0) if d == 0 else (rng.randint(-d, d), rng.randint(-d, d))
            base = NEIGHBOUR_OFFSETS + n * 16
            fields += [(base, dx & 0xFFFFFFFF, 4), (base + 4, dy & 0xFFFFFFFF, 4), (base + 8, d, 4)]
            n += 1
    return fields


def teams(rng):
    fields = [(LOCAL_PLAYER, rng.randrange(20), 4)]
    for p in range(20):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, rng.randrange(3), 4))
    return fields


def sight_bytes(rng, centres):
    """Small sight values on and around the given centres."""
    fields = []
    for x, y in centres:
        for dx in range(-1, 2):
            for dy in range(-1, 2):
                fields.append((tile((x + dx) & 0xFFFFFFFF, (y + dy) & 0xFFFFFFFF) + 0xd,
                               rng.choice([0, 0, rng.randrange(0x40)]), 1))
    return fields


def sight_owner(rng):
    return 0xff if rng.random() < 0.2 else rng.randrange(20)


def reveal_points(rng, pool, args):
    size = rng.randint(16, 48)
    high = rng.randrange(-2, 8)
    fields = [(SIGHT_MAP, size, 4), (REVEAL_COUNT, high & 0xFFFFFFFF, 4)] + ring_table(rng, 0x24)
    centres = []
    for i in range(max(high + 1, 0)):
        x, y = rng.randint(-2, size + 2), rng.randint(-2, size + 2)
        centres.append((x, y))
        base = REVEAL_POINTS + i * 16
        fields += [(base, 0 if rng.random() < 0.3 else rng.randrange(1, 256), 1),
                   (base + 4, x & 0xFFFFFFFF, 4), (base + 8, y & 0xFFFFFFFF, 4)]
    return fields + sight_bytes(rng, centres)


def building_sight(rng, pool, args):
    size = rng.randint(16, 48)
    high = rng.randrange(0, 80)
    fields = [(SIGHT_MAP, size, 4), (BUILDING_COUNT, high, 4)] + ring_table(rng, 0x37) + teams(rng)
    centres = []
    for i in range(args[0], high + 1, 16):
        x, y = rng.randrange(size + 2), rng.randrange(size + 2)
        centres.append((x, y))
        base = BUILDINGS + i * BUILDING_BYTES
        fields += [(base, sight_owner(rng), 1), (base + 2, x, 2), (base + 4, y, 2),
                   (base + 6, rng.choice([6, 0xf, 0x10, 0x12, 1, 0x20]), 1),
                   (base + 8, rng.choice([0, 0, 1, 3]), 2),
                   (base + 0x21, 0 if rng.random() < 0.8 else rng.randrange(256), 1)]
    return fields + sight_bytes(rng, centres)


def settler_sight(rng, pool, args):
    size = rng.randint(16, 48)
    high = rng.randrange(0, 80)
    fields = [(SIGHT_MAP, size, 4), (SETTLER_COUNT, high, 4)] + ring_table(rng, 0x1e) + teams(rng)
    centres = []
    for i in range(args[0], high + 1, 16):
        x, y = rng.randrange(size + 2), rng.randrange(size + 2)
        centres.append((x, y))
        base = SETTLERS + i * SETTLER_BYTES
        fields += [(base, sight_owner(rng), 1), (base + 4, x, 2), (base + 6, y, 2),
                   (base + 1, rng.choice([0x17, 0x17, 0x25, 0x27, 0x29, 0x2a, 0x2b, 0x2c, 1, 0x30]), 1)]
    return fields + sight_bytes(rng, centres)


def seen_tile_fields(rng, x, y):
    t = tile(x, y)
    kind = rng.randrange(4)
    flags = rng.getrandbits(8)
    fields = [(t + 0x6, 0x1c if rng.random() < 0.3 else rng.randrange(256), 1),
              (t + 0x7, 0 if rng.random() < 0.3 else rng.randrange(256), 1)]
    if kind == 0:
        number = rng.randrange(1, 4000)
        b = BUILDINGS + number * BUILDING_BYTES
        total = rng.randrange(8)
        fields += [(t + 0x2, number, 2), (t + 0xa, flags | 0x20, 1),
                   (b + 0x21, 0 if rng.random() < 0.3 else rng.randrange(256), 1),
                   (b + 0x1f, total, 1), (b + 0x20, rng.randrange(total + 1), 1)]
    elif kind == 1:
        number = rng.randrange(1, 32000)
        fields += [(t + 0x2, number, 2), (t + 0xa, flags & ~0x20, 1),
                   (PILES + number * PILE_BYTES + 1, 0 if rng.random() < 0.3 else rng.randrange(256), 1)]
    else:
        fields += [(t + 0x2, 0, 2), (t + 0xa, flags, 1)]
    return fields


def seen_tile(rng, pool, args):
    return seen_tile_fields(rng, args[0], args[1])


def fog_slice(rng, pool, args):
    size = rng.choice([16, 32, rng.randint(16, 40)])
    fields = [(SIGHT_MAP, size, 4)]
    rows = size // 16
    for x in range(size):
        for y in range(args[0] * rows, (args[0] + 1) * rows):
            t = tile(x, y)
            fields += [(t + 0xd, rng.choice([0, 1, 2, 3, rng.randrange(10)]), 1), (t + 0x9, rng.getrandbits(8), 1)]
            if rng.random() < 0.3:
                fields += seen_tile_fields(rng, x, y)
    return fields


def expiring_effects(rng, pool, args):
    """A consistent effect table: up to 40 cells with lists of 0..4 effects,
    durations around the tick (args[0]) and -1."""
    bx, by = rng.randint(1, 8), rng.randint(1, 5)
    cells = bx * by
    count16 = rng.randrange(-1, 2) if rng.random() < 0.1 else rng.randint(1, cells + 3)
    fields = [(0x668a4, bx, 4), (0x668a8, by, 4), (0x668b0, count16 & 0xFFFFFFFF, 4),
              (0x668ac, rng.randrange(cells + 2), 4)]
    slot = 1
    for cell in range(cells + 1):
        previous = 0
        head = 0
        for _ in range(rng.randrange(5)):
            base = slot * 0x4c
            duration = 0xffff if rng.random() < 0.25 else rng.randint(-5, 60) & 0xffff
            fields += [(base, (args[0] - rng.randrange(60)) & 0xFFFFFFFF, 4), (base + 4, duration, 2),
                       (base + 0x40, cell, 4), (base + 0x44, previous, 4), (base + 0x48, 0, 4)]
            if previous:
                fields.append((previous * 0x4c + 0x48, slot, 4))
            else:
                head = slot
            previous = slot
            slot += 1
        fields.append((EFFECT_HEADS + cell * 4, head, 4))
    return fields


def effect_timing(rng, pool, args):
    start = rng.getrandbits(32) if rng.random() < 0.3 else (args[0] - rng.randrange(-5, 60)) & 0xFFFFFFFF
    duration = 0xffff if rng.random() < 0.3 else (rng.getrandbits(16) if rng.random() < 0.3 else rng.randrange(60))
    return [(0, start, 4), (4, duration, 2)]


def traffic_tile(rng, pool, args):
    t = tile(args[0], args[1])
    return [(t + 0x12, rng.choice([0, 7, 8, 9, rng.randrange(256)]), 1),
            (t + 0x6, 0x1c if rng.random() < 0.5 else rng.randrange(256), 1), (t + 0xa, rng.getrandbits(8), 1)]


def spell_number(rng, pool):
    return rng.randrange(40)


SPECS += [
    Spec("CGameData_MarkRevealPointsSight", 0x00518f20, "thiscall", [], eax_mask=0, presets=reveal_points,
         max_cases=300),
    Spec("CGameData_MarkBuildingSight", 0x00519fa0, "thiscall", [below(16, 0.02)], eax_mask=0,
         presets=building_sight, max_cases=300),
    Spec("CGameData_MarkSettlerSight", 0x0051a150, "thiscall", [below(16, 0.02)], eax_mask=0,
         presets=settler_sight, max_cases=300),
    Spec("CGameData_SeenObjectAt", 0x0051d5f0, "thiscall", [below(64), below(64)], eax_mask=0xffff,
         presets=seen_tile),
    # Exempt: 0x519186 (level 7 when 0x7dfd84 is set, re-read inside the
    # branch taken only when it was 0): dead unless another thread changes it.
    Spec("CGameData_UpdateFogSlice", 0x00519010, "thiscall", [below(16)], eax_mask=0, presets=fog_slice,
         globals=lambda rng, pool, args: [(0x007dfd84, 1 if rng.random() < 0.3 else 0, 4)], max_cases=200,
         exempt_blocks=(0x519186,)),
    Spec("MapEffect_IsExpired", 0x005440c0, "thiscall", [between(900, 1100)], eax_mask=0xff,
         presets=effect_timing),
    Spec("MapEffects_ExpireSome", 0x00545d40, "thiscall", [between(1000, 1100)], eax_mask=0,
         presets=expiring_effects, max_cases=300),
    Spec("CGameData_DecayTraffic", 0x00503980, "thiscall", [below(64), below(64)], eax_mask=0,
         presets=traffic_tile),
    Spec("CGameData_PayForSpell", 0x0054b2b0, "thiscall", [below(20), spell_number], eax_mask=0,
         presets=mana),
]


WORLD_EFFECTS = 0x14d2bc8  # the map effect table inside the world


def world_effect_table(rng, pool, args):
    """The usual effect table, moved to its place in the world, and a map size."""
    fields = [(WORLD_EFFECTS + offset, value, size) for offset, value, size in effect_table(0.0)(rng, pool, args)]
    return fields + [(SIGHT_MAP, rng.randint(16, 384), 4)]


def spell_coordinate(rng, pool) -> int:
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(400)


SPECS += [
    Spec("CGameData_SpawnSpellEffects", 0x005474e0, "thiscall",
         [anything, spell_coordinate, spell_coordinate, anything, below(34, 0.05)], eax_mask=0,
         presets=world_effect_table, ignore_bytes=tuple(WORLD_EFFECTS + b for b in EFFECT_TABLE_PADDING),
         globals=lambda rng, pool, args: [(0x007dfd48, rng.getrandbits(32), 4)], max_cases=400),
]


# Package B, batch 5 (soldier.cpp: UpdateSoldier). Package C's ClearTileOwner
# 0x51b640 and RecalculateTerritory 0x519a90 (verified on their own) are
# probed: the original and ours call them through their addresses, and the
# territory recount needs a consistent world. Exempt: 0x50fde8 (a tower
# captured by a class-2 attacker, job 0x8c): dead. A capture needs a victim
# in job 0xad/0xae, and bowmen can attack neither (0x50eced..0x50ed61) nor
# the job-0x130 targets whose hit goes to a nearby settler (0x50ed63).
# About 10 of 1500 cases fault in both (random ring indices of
# PushUiFeedback / area ids of AssignTileToArea in the lazily random
# world, and garrison walks of random 0xae jobs that never end): invalid.
SOLDIER_PROBES = {0x0051b640: (2, "thiscall", "probe"), 0x00519a90: (2, "thiscall", "probe")}
SOLDIER_TYPES_B = [5, 0x20, 0x23, 8, 0x1e, 0x21, 0x0f, 0x1f, 0x22]
TARGET_TYPES_B = [5, 0x20, 8, 0x1e, 0x0f, 0x22, 0x16, 0x17, 0x25, 0x27, 0x28, 0x29, 0x2a, 1]
TARGET_JOBS_B = [0x37, 0x8e, 0x35, 0xb0, 3, 4, 0xad, 0xae, 0xad, 0xae, 0x130, 0xd4, 0x12f, 0x135, 0x10, 0x55, 0x151]
MAP_EFFECTS = 0x14d2bc8
PLAYER_LIST_LENGTHS = 0x11940bc


def soldier_offsets(rng):
    """The offset table up to radius 20 with distances and random sectors, then a stop."""
    fields = []
    points = sorted(((dx, dy) for dx in range(-21, 22) for dy in range(-21, 22)), key=lambda p: (hex_distance(*p), p))
    points = [p for p in points if hex_distance(*p) <= 20]
    for k, (dx, dy) in enumerate(points):
        base = NEIGHBOUR_OFFSETS + k * 16
        fields += [(base, dx & 0xFFFFFFFF, 4), (base + 4, dy & 0xFFFFFFFF, 4),
                   (base + 8, hex_distance(dx, dy), 4), (base + 12, rng.randrange(6), 4)]
    fields.append((NEIGHBOUR_OFFSETS + len(points) * 16 + 8, 100, 4))
    return fields


SOLDIER_CLASS_TYPES = {1: [5, 0x20, 0x23], 2: [8, 0x1e, 0x21], 3: [0x0f, 0x1f, 0x22]}


def soldier_kill_scenario(rng, soldier, x, y):
    """One enemy within reach that dies of the hit: a worker in job 3 of a
    building (class counters), a tower soldier in job 0xae in a garrison
    list of 1..4 (the list walk; the tower falls when it was the last) or
    in job 0xad of an empty tower (capture, with building soldiers in the
    jobs the capture loop clears or keeps). Attackers of all classes,
    with and without an idle-list predecessor."""
    kind = rng.choice(["worker", "garrison", "tower"])
    cls = rng.choice([1, 2, 3]) if kind == "worker" else rng.choice([1, 3])
    record = SETTLERS + soldier * SETTLER_BYTES
    a_owner, e_owner = rng.sample(range(4), 2)
    fields = [(record + 0x0, a_owner, 1), (record + 0x1, rng.choice(SOLDIER_CLASS_TYPES[cls]), 1),
              (record + 0xe, rng.choice([0x8e, 0x37, 0x35, 0x17, 0x8d]), 2),
              (record + 0x14, rng.getrandbits(8) & 0xdf, 1),
              (record + 0x12, 0 if rng.random() < 0.5 else rng.randrange(1, 0x400), 2),
              (PLAYER_RECORDS + a_owner * PLAYER_RECORD_BYTES + 0xc, 0, 4),
              (PLAYER_RECORDS + e_owner * PLAYER_RECORD_BYTES + 0xc, 1, 4)]
    distance = rng.randint(6, 13) if cls == 2 else (1 if kind != "worker" or cls == 1 else rng.randint(1, 2))
    if cls == 2 and rng.random() < 0.4:
        # A bowman in a tower (job 0x8c: range 22, minimum 10; arrows from
        # the tower slot's offset).
        fields.append((record + 0xe, 0x8c, 2))
        distance = rng.randint(10, 20)
    dx, dy = rng.choice([(dx, dy) for dx in range(-20, 21) for dy in range(-20, 21) if hex_distance(dx, dy) == distance])
    ex, ey = x + dx, y + dy
    ids = rng.sample([i for i in range(1, 0x400) if i != soldier], 6)
    e, nodes = ids[0], ids[1:]
    r = SETTLERS + e * SETTLER_BYTES
    building = rng.randrange(1, 0x40)
    b = BUILDINGS + building * BUILDING_BYTES
    job = {"worker": 3, "garrison": 0xae, "tower": 0xad}[kind]
    fields += [(tile(ex, ey), e, 2), (r + 0x0, e_owner, 1), (r + 0x1, rng.choice([5, 8, 0x0f, 0x20, 0x1e, 0x1f]), 1),
               (r + 0x2, rng.randrange(3), 1), (r + 0x3, 0, 1), (r + 0x4, ex, 2), (r + 0x6, ey, 2),
               (r + 0xe, job, 2), (r + 0x14, rng.getrandbits(8), 1), (r + 0x22, building, 2),
               (b + 0x0, e_owner, 1), (b + 0x2, rng.randrange(40, 340), 2), (b + 0x4, rng.randrange(40, 340), 2),
               (b + 0x6, rng.choice([0x10, 0x11, 0x12]), 1), (b + 0x7, rng.randrange(3), 1),
               (b + 0x19, rng.randrange(1, 4), 1), (b + 0x1b, rng.randrange(1, 4), 1), (b + 0x1f, rng.randrange(1, 4) << 4 | rng.randrange(16), 1)]
    if kind == "garrison":
        count = rng.randint(1, 4)
        chain = nodes[:count - 1]
        chain.insert(rng.randrange(count), e)
        fields.append((b + 0x8, chain[0], 2))
        for here, after in zip(chain, chain[1:] + [0]):
            fields.append((SETTLERS + here * SETTLER_BYTES + 0x10, after, 2))
    elif kind == "tower":
        fields.append((b + 0x8, 0, 2))
    if kind != "worker":
        # Soldiers and workers of the tower, some in jobs the capture keeps.
        for k in rng.sample([i for i in range(1, 40) if i not in (soldier, e)], 4):
            q = SETTLERS + k * SETTLER_BYTES
            fields += [(q + 0x0, rng.randrange(4), 1), (q + 0x22, building, 2), (q + 0x1, rng.choice([5, 8, 0x0f]), 1),
                       (q + 0xe, rng.choice([3, 4, 0x10, 0x6a, 0x8b, 5, 0x8b]), 2)]
        fields.append((SETTLER_HIGH_WATER, 40, 4))
    return fields


def soldier_state(rng, pool, args):
    soldier = args[0]
    record = SETTLERS + soldier * SETTLER_BYTES
    x, y = rng.randrange(40, 340), rng.randrange(40, 340)
    owner = rng.randrange(4)
    art = rng.choice(SOLDIER_TYPES_B)
    job = rng.choice([0x8c, 0xae, 0x8e, 0x35, 0x37, 0x17, 0x8d])
    slot = (rng.randrange(3) << 5) | (rng.randrange(3) << 3) | rng.randrange(4)
    fields = [(MAP_SIZE, 384, 4), (SETTLER_HIGH_WATER, rng.randrange(40), 4), (0xde7444, 3, 4),
              (record + 0x0, owner, 1), (record + 0x1, art, 1), (record + 0x2, rng.randrange(3), 1),
              (record + 0x4, x, 2), (record + 0x6, y, 2), (record + 0xe, job, 2),
              (record + 0x14, rng.getrandbits(8) & (0xff if rng.random() < 0.2 else 0xdf), 1),
              (record + 0x15, slot, 1), (record + 0x10, rng.randrange(0x400), 2),
              (record + 0x12, 0 if rng.random() < 0.3 else rng.randrange(0x400), 2),
              (record + 0xc, 0 if rng.random() < 0.2 else rng.getrandbits(16), 2),
              (tile(x, y), soldier, 2), (0x32870, rng.randrange(1000), 4), (0x32874, rng.randrange(1000), 4),
              (0x366f8, rng.randrange(1000), 4), (0x366fc, rng.randrange(1000), 4)]
    fields += soldier_offsets(rng)
    for p in range(20):
        base = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(base + 0x0, rng.choice([1, 2]), 4), (base + 0x4, rng.randrange(3), 4),
                   (base + 0xc, rng.randrange(3), 4),
                   (base + 0x80, rng.randrange(0x20) if rng.random() < 0.1 else rng.randrange(0xe0, 0x140), 4)]
    for p in range(20):
        for lst in range(10):
            fields.append((PLAYER_LIST_LENGTHS + (p * 10 + lst) * 4, rng.randrange(4), 4))
    # Tiles around: owners, walk regions, mostly no settler; presence blocks
    # are lazily random (IsOtherPlayerNear mostly true).
    for dx in range(-21, 22):
        for dy in range(-21, 22):
            if (dx, dy) == (0, 0):
                continue
            t = tile(x + dx, y + dy)
            fields += [(t + 0x0, 0, 2), (t + 0x8, rng.choice([0, 1, 2, 3, 0xff]), 1),
                       (t + 0x10, rng.randrange(1, 3), 1)]
    fields += [(tile(x, y) + 0x8, rng.choice([0, 1, 2, 3, 0xff]), 1), (tile(x, y) + 0x10, rng.randrange(1, 3), 1)]
    if rng.random() < 0.4:
        fields += soldier_kill_scenario(rng, soldier, x, y)
        enemies = 0
    else:
        enemies = rng.randrange(1, 6)
    for i in range(enemies):
        reach = 3 if rng.random() < 0.6 else 20
        ex, ey = x + rng.randint(-reach, reach), y + rng.randint(-reach, reach)
        if (ex, ey) == (x, y):
            continue
        e = rng.randrange(1, 0x400)
        if e == soldier:
            continue
        r = SETTLERS + e * SETTLER_BYTES
        ejob = rng.choice(TARGET_JOBS_B)
        etype = rng.choice(TARGET_TYPES_B)
        if rng.random() < 0.1:
            etype, ejob = 0x27, 0x135
        elif rng.random() < 0.1:
            etype, ejob = rng.choice([0x16, 0x28]), rng.choice([0xd4, 0xd5, 0x12e, 0x12f, 0x35, 0xb0])
        building = rng.randrange(1, 0x40)
        b = BUILDINGS + building * BUILDING_BYTES
        fields += [(tile(ex, ey), e, 2), (r + 0x0, rng.randrange(4), 1), (r + 0x1, etype, 1),
                   (r + 0x2, rng.randrange(3), 1), (r + 0x3, rng.randrange(8) if rng.random() < 0.6 else rng.getrandbits(8), 1),
                   (r + 0x4, ex, 2), (r + 0x6, ey, 2), (r + 0xe, ejob, 2), (r + 0x14, rng.getrandbits(8), 1),
                   (r + 0x22, building, 2), (r + 0x10, rng.choice([0, rng.randrange(1, 0x400)]), 2),
                   (r + 0x12, rng.choice([0, rng.randrange(1, 0x400)]), 2), (r + 0x24, ex, 2), (r + 0x26, ey, 2),
                   (b + 0x2, rng.randrange(40, 340), 2), (b + 0x4, rng.randrange(40, 340), 2),
                   (b + 0x6, rng.choice([0x10, 0x11, 0x12]), 1), (b + 0x7, rng.randrange(3), 1),
                   (b + 0x0, rng.randrange(4), 1)]
        other = 1 + (e + 1) % 0x3ff
        # Soldiers of the building (cleared when the tower falls).
        for k in range(1, 40):
            if rng.random() < 0.15:
                q = SETTLERS + k * SETTLER_BYTES
                fields += [(q + 0x0, rng.randrange(4), 1), (q + 0x22, building, 2),
                           (q + 0x1, rng.choice([5, 8, 0x0f, 1]), 1), (q + 0xe, rng.choice([3, 4, 0x10, 0x6a, 0x8b, 5]), 2)]
        # A type-0x29 unit next to the soldier (FindNearbyOwnCatapult for job 0x130 targets).
        if ejob == 0x130 and rng.random() < 0.5:
            u = 1 + (e + 7) % 0x3ff
            ux, uy = x + rng.choice([-1, 1]), y
            q = SETTLERS + u * SETTLER_BYTES
            fields += [(tile(ux, uy), u, 2), (q + 0x1, 0x29, 1), (q + 0x0, rng.randrange(4), 1), (q + 0x3, 0, 1),
                       (q + 0x4, ux, 2), (q + 0x6, uy, 2), (q + 0xe, 0x55, 2)]
        if ejob == 0xae:
            empty = rng.random() < 0.5
            if empty:
                fields += [(b + 0x8, e, 2), (r + 0x10, 0, 2)]
            elif rng.random() < 0.5:
                fields += [(b + 0x8, other, 2), (SETTLERS + other * SETTLER_BYTES + 0x10, e, 2)]
            else:
                third = 1 + (e + 2) % 0x3ff
                fields += [(b + 0x8, other, 2), (SETTLERS + other * SETTLER_BYTES + 0x10, third, 2),
                           (SETTLERS + third * SETTLER_BYTES + 0x10, e, 2)]
        elif ejob == 0xad:
            fields.append((b + 0x8, 0 if rng.random() < 0.5 else other, 2))
    # The effect table: 20 slots in use, empty cell lists, 80 blocks per row.
    fields += [(MAP_EFFECTS + 0x668a0, 20, 4), (MAP_EFFECTS + 0x668a4, 80, 4)]
    fields += [(MAP_EFFECTS + EFFECT_HEADS + c * 4, 0, 4) for c in range(1400)]
    fields += [(MAP_EFFECTS + s * 0x4c, 1, 4) for s in range(1, 21)]
    return fields


def soldier_globals(rng, pool, args):
    return [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4), (CAMPAIGN_MISSION, rng.choice([0, 5]), 4),
            (0x007a7af0 + 0x189, rng.randrange(2), 1)]


def soldier_gfx(rng, pool, args):
    """Every animation the soldiers use, for races 0..2 (fake graphics object)."""
    fields = []
    for kind in list(range(0x22, 0x46)) + [0x46] + list(range(0x89, 0x9b)):
        for race in range(3):
            fields += gfx_sprite(animation_table()[kind * 4 + race], rng)
    return fields


def soldier_presets(rng, pool, args):
    return soldier_gfx(rng, pool, args) + soldier_state(rng, pool, args)


SPECS += [
    Spec("CGameData_UpdateSoldier", 0x0050e9f0, "thiscall", [between(1, 0x3ff)], presets=soldier_presets,
         globals=soldier_globals, callees=SOLDIER_PROBES, ignore_bytes=tuple(MAP_EFFECTS + s * 0x4c + p
                                                                          for s in range(1, 40)
                                                                          for p in EFFECT_PADDING),
         max_cases=1500, exempt_blocks=(0x50fde8,), memory_mb=1600),  # ~1.6 GB per worker
]


# --- package D: CGameData::UpdateEnvironment (environment.cpp) ---------------
#
# Callees with their own specs that need a consistent world (or loop over
# whole record tables) are probed; Random, RandomBelow6, IsOnMap,
# NeighbourX/Y, TileScreenX/Y and DecayTraffic run.

ENV_CALLEES = {
    0x005052c0: (1, "thiscall", "probe"),  # DispatchPileCarrier
    0x005474e0: (5, "thiscall", "probe"),  # SpawnSpellEffects
    0x005480b0: (5, "thiscall", "probe"),  # ExecuteSpell (GodlyEvents dispatcher)
    0x004e8ba0: (3, "thiscall", "probe"),  # QueuePendingEvent
    0x004fec10: (4, "thiscall", "probe"),  # NotifyLocalPlayer
    0x004d9250: (1, "thiscall", "probe"),  # CrushBuilding
    0x00544f80: (5, "thiscall", "probe"),  # MapEffects::Spawn_544f80
    0x00545070: (5, "thiscall", "probe"),  # MapEffects::Spawn_545070
    0x004ffb40: (3, "thiscall", "probe"),  # SubtractPlayerCounter
    0x00518ec0: (1, "thiscall", "probe"),  # ClearSightSlice
    0x00519fa0: (1, "thiscall", "probe"),  # MarkBuildingSight
    0x00518f20: (0, "thiscall", "probe"),  # MarkRevealPointsSight
    0x0051a150: (1, "thiscall", "probe"),  # MarkSettlerSight
    0x00519010: (1, "thiscall", "probe"),  # UpdateFogSlice
    0x004fb520: (1, "thiscall", "probe"),  # minimap slice
    0x0051b770: (2, "thiscall", "probe"),  # UnblockTile
    0x00548380: (3, "thiscall", "probe"),  # spell on an area (GodlyEvents)
    0x00545d40: (1, "thiscall", "probe"),  # MapEffects::ExpireSome
    0x004ff470: (0, "thiscall", "probe"),  # ShiftStatistics
}

EFFECTS = 0x36700
EFFECT_HIGH_WATER = 0x67440
AREAS = 0xe145e8
AREA_BYTES = 0x2e0
AREA_HIGH_WATER = 0xe115dc
LANDSCAPE_X, LANDSCAPE_Y, LANDSCAPE_STEP = 0x2d2f4, 0x2d2f8, 0x2d2fc
ENV_OBJECTS = [0, 0x44, 0x4a, 0x52, 0x53, 0x54, 0x55, 0x5c, 0x5d, 0x5e, 0x66, 0x6e, 0x6f, 0xb8, 0xcf, 0xd0,
               0xd7, 0xd8, 0xdf, 0xe0, 0xe7, 0xe8, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfc, 0xfd, 0xfe]


def env_tile(rng, x, y, trees: float):
    t = tile(x & 0xFFFFFFFF, y & 0xFFFFFFFF)
    obj = rng.choice([0x44, 0x4a, 0x52, 0x53]) if rng.random() < trees else rng.choice(ENV_OBJECTS)
    return [(t + 0x7, obj, 1), (t + 0x6, rng.choice([0x16, 0x16, 0x10, 0x05, rng.randrange(256)]), 1),
            (t + 0x11, rng.choice([0, 1, 5, 0xf, 0x10, rng.randrange(256)]), 1),
            (t + 0x0, 0 if rng.random() < 0.5 else rng.randrange(1, 32000), 2),
            (t + 0x14, rng.randrange(8), 2)]


def update_environment(rng, pool, args):
    size = rng.choice([44, 45, 64, 90, rng.randint(45, 100)])
    fields = [(SIGHT_MAP, size, 4)]
    # Ring table: 201 small offsets (the tree count and the fire spread use
    # entries 0..200 and 19..60).
    for n in range(205):
        base = NEIGHBOUR_OFFSETS + n * 16
        d = min(n // 6 + 1, 5)
        fields += [(base, rng.randint(-d, d) & 0xFFFFFFFF, 4), (base + 4, rng.randint(-d, d) & 0xFFFFFFFF, 4)]
    # The tiles the landscape pass visits, their neighbours and a few more.
    x, y = rng.randrange(size), rng.randrange(size)
    step = rng.randint(1, size)
    fields += [(LANDSCAPE_X, x, 4), (LANDSCAPE_Y, y, 4), (LANDSCAPE_STEP, step, 4)]
    trees = rng.choice([0.0, 0.1, 0.5])
    for _ in range(size * size // 2000):
        x += step
        if x >= size:
            x -= size
            y += 1
            if y >= size:
                y -= size
        for dx in range(-5, 6):
            for dy in range(-5, 6):
                if abs(dx) <= 1 and abs(dy) <= 1 or rng.random() < 0.3:
                    fields += env_tile(rng, x + dx, y + dy, trees)
    # Piles: every 64th from tick & 63.
    tick = 220 * rng.randrange(100) if rng.random() < 0.2 else rng.getrandbits(32)
    pile_count = rng.randrange(200)
    fields.append((PILE_COUNT, pile_count, 4))
    for n in range(tick & 0x3f, pile_count + 1, 0x40):
        base = PILES + n * PILE_BYTES
        px, py = rng.randrange(size), rng.randrange(size)
        area = rng.randrange(1, 8)
        good = rng.randrange(36)
        fields += [(base, good, 1), (base + 1, rng.randrange(8), 1), (base + 2, rng.randrange(8), 1),
                   (base + 4, 0xfe if rng.random() < 0.7 else rng.randrange(256), 1), (base + 6, px, 2),
                   (base + 8, py, 2), (tile(px, py) + 0x14, area, 2),
                   (AREAS + area * AREA_BYTES + 0x21a + good, rng.randrange(3), 1)]
    # Areas 1..n with short pile lists of goods 10 and 28 (piles 1000..).
    areas = rng.randrange(0, 8)
    fields.append((AREA_HIGH_WATER, areas, 4))
    list_pile = 1000
    for a in range(1, 8):
        base = AREAS + a * AREA_BYTES
        fields.append((base, 0xff if rng.random() < 0.2 else rng.randrange(20), 1))
        for good in (10, 28):
            head = 0
            for _ in range(rng.randrange(3)):
                fields += [(PILES + list_pile * PILE_BYTES + 0xc, head, 2),
                           (PILES + list_pile * PILE_BYTES + 1, rng.randrange(256), 1)]
                head = list_pile
                list_pile += 1
            fields.append((base + 0xa + 2 * good, head, 2))
    for p in range(20):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, rng.choice([0, 1, 1, 2, 3]), 4))
    fields.append((0x1195dcc, rng.choice([0, 1]), 1))
    # Timed events.
    events = rng.randrange(0, 24)
    fields.append((EFFECT_HIGH_WATER, events, 4))
    passenger = 20000
    for e in range(1, events + 1):
        base = EFFECTS + e * 0x14
        kind = rng.randrange(9)
        unit = rng.randrange(1, 4000) if kind in (4, 5) else rng.randrange(1, 4000)
        fields += [(base, kind, 2), (base + 2, rng.randrange(3), 2), (base + 4, unit, 2),
                   (base + 6, rng.choice([0, 5, 100, rng.randrange(0x10000)]), 2), (base + 8, rng.getrandbits(16), 2),
                   (base + 10, rng.getrandbits(16), 2), (base + 12, rng.getrandbits(16), 2)]
        if kind in (5, 4):
            b = BUILDINGS + unit * BUILDING_BYTES
            fields += [(b + 0x38, rng.choice([0, 5, 200, rng.randrange(0x10000)]), 2)]
        if kind in (6, 7):
            s = SETTLERS + unit * SETTLER_BYTES
            art = rng.choice([0x29, 0x2b, 0x2c, 0x01])
            fields += [(s, 0xff if rng.random() < 0.15 else rng.randrange(20), 1), (s + 1, art, 1),
                       (s + 3, rng.choice([0, 5, 200, rng.randrange(256)]), 1),
                       (s + 4, rng.randrange(1, size - 1), 2), (s + 6, rng.randrange(1, size - 1), 2),
                       (s + 0x12, 0 if rng.random() < 0.5 else rng.randrange(1, 4000), 2),
                       (s + 0x17, rng.getrandbits(8), 1), (s + 0x18, rng.getrandbits(8), 1)]
            chain = 0
            if art == 0x2c:
                for _ in range(rng.randrange(3)):
                    fields += [(SETTLERS + passenger * SETTLER_BYTES + 0x10, chain, 2),
                               (SETTLERS + passenger * SETTLER_BYTES + 0x12, rng.randrange(4000), 2)]
                    chain = passenger
                    passenger += 1
            else:
                chain = 0 if rng.random() < 0.5 else rng.randrange(1, 4000)
            fields.append((s + 0x10, chain, 2))
    # Reveal points.
    reveal = rng.randrange(50)
    fields.append((REVEAL_COUNT, reveal, 4))
    for i in range(reveal + 1):
        fields.append((REVEAL_POINTS + i * 16 + 0xc, rng.randint(-2, 3) & 0xFFFFFFFF, 4))
    fields += [(0x110a580, rng.randrange(0x38), 4), (0x110a584, rng.randrange(0x41), 4)]
    update_environment.tick = tick
    return fields


def update_environment_globals(rng, pool, args):
    return [(0x007dfd48, update_environment.tick, 4), (0x007dfd30, 1 if rng.random() < 0.2 else 0, 4),
            (0x007acfa4, rng.randrange(20), 4), (0x007deb55, 1 if rng.random() < 0.05 else 0, 1)]


SPECS += [
    Spec("CGameData_UpdateEnvironment", 0x004e7810, "thiscall", [], eax_mask=0, presets=update_environment,
         globals=update_environment_globals, callees=ENV_CALLEES, max_cases=600),
]


# --- package D: the minimap (minimap.cpp) ------------------------------------

def minimap_globals(rng, pool, args):
    shifts, scales = rng.choice([((11, 5, 0), (31, 63, 31)), ((10, 5, 0), (31, 31, 31)), ((16, 8, 0), (255, 255, 255))])
    fields = [(0x00730f58, rng.choice([0, 0, 1, 2]), 4), (0x00730f5c, rng.randrange(2), 4),
              (0x00730f60, rng.choice([0, 1, 2, 2]), 4)]
    fields += [(0x007dfba0 + 4 * i, v, 4) for i, v in enumerate(shifts)]
    fields += [(0x007dfbac + 4 * i, v, 4) for i, v in enumerate(scales)]
    return fields


def minimap_tile_fields(rng, x, y):
    t = tile(x & 0xFFFFFFFF, y & 0xFFFFFFFF)
    settler = 0 if rng.random() < 0.6 else rng.randrange(1, 4000)
    fields = [(t + 0x9, rng.getrandbits(8), 1), (t + 0xa, rng.getrandbits(8), 1),
              (t + 0x8, 0xff if rng.random() < 0.3 else rng.randrange(20), 1), (t + 0x0, settler, 2),
              (t + 0x6, rng.choice([0x05, 0x10, 0x25, 0x35, 0x55, 0x85, 0x95, rng.randrange(256)]), 1)]
    if settler:
        s = SETTLERS + settler * SETTLER_BYTES
        fields += [(s, rng.randrange(20), 1), (s + 1, rng.choice([0x17, 0x17, 5, 8, 15, 22, 0, 1]), 1),
                   (s + 0x14, rng.getrandbits(8), 1)]
    return fields


def minimap_pixel(rng, pool, args):
    x0, y0, x1, y1 = (a - (1 << 32) if a >= 1 << 31 else a for a in args)
    fields = [(LOCAL_PLAYER, rng.randrange(20), 4)]
    fields += [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, rng.randrange(3), 4) for p in range(20)]
    for x in range(x0, max(x0, x1) + 1):
        for y in range(y0, max(y0, y1) + 1):
            fields += minimap_tile_fields(rng, x, y)
    return fields


def minimap_slice(rng, pool, args):
    size = rng.randint(64, 160)
    return [(SIGHT_MAP, size, 4), (LOCAL_PLAYER, rng.randrange(20), 4)] + \
           [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, rng.randrange(3), 4) for p in range(20)]


def minimap_slice_globals(rng, pool, args):
    return minimap_globals(rng, pool, args) + [(0x00730f28, rng.choice([16, 32, 64, rng.randint(16, 64)]), 4)]


class _Corners:
    """Generates (x0, y0, x0 + d, y0 + d') as four consecutive arguments."""
    def __init__(self):
        self.last = 0

    def first(self, rng, pool):
        self.last = rng.randrange(1, 60)
        return self.last

    def second(self, rng, pool):
        return (self.last + rng.randint(-1, 3)) & 0xFFFFFFFF


_cx, _cy = _Corners(), _Corners()

SPECS += [
    # Exempt: 0x4fbc79/0x4fbc8c (mode != 0, settlers option 1: a hidden type
    # 0x17 settler of another team takes the tile's owner): dead, such a
    # settler was skipped just before (0x4fbc33).
    Spec("CGameData_MinimapPixel", 0x004fb720, "thiscall", [_cx.first, _cy.first, _cx.second, _cy.second],
         presets=minimap_pixel, globals=minimap_globals, max_cases=3000, exempt_blocks=(0x4fbc79, 0x4fbc8c)),
    Spec("CGameData_UpdateMinimapSlice", 0x004fb520, "thiscall", [below(64)], eax_mask=0, presets=minimap_slice,
         globals=minimap_slice_globals, max_cases=150),
]


# --- package D: spells (spells.cpp) -------------------------------------------

def spell_area(rng, pool, args):
    """Player args[0] with some mana, a small map, a ring table of 320 small
    offsets and interesting tiles around (args[1], args[2])."""
    size = rng.randint(32, 96)
    fields = [(SIGHT_MAP, size, 4), (LOCAL_PLAYER, rng.randrange(4), 4)]
    record = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    fields += [(record + 0x4, rng.randrange(4), 4), (record + 0x8c, rng.randrange(100), 4)]
    fields += [(record + 0x94 + slot * 4, rng.randrange(60), 4) for slot in range(9)]
    offsets = []
    for n in range(320):
        d = 1 + n // 12
        dx, dy = rng.randint(-d, d), rng.randint(-d, d)
        offsets.append((dx, dy))
        fields += [(NEIGHBOUR_OFFSETS + n * 16, dx & 0xFFFFFFFF, 4), (NEIGHBOUR_OFFSETS + n * 16 + 4, dy & 0xFFFFFFFF, 4)]
    x, y = (a - (1 << 32) if a >= 1 << 31 else a for a in args[1:3])
    seen = set()
    for dx, dy in offsets:
        tx, ty = x + dx, y + dy
        if 0 <= tx < size and 0 <= ty < size and (tx, ty) not in seen:
            seen.add((tx, ty))
            t = tile(tx, ty)
            fields += [(t + 0x7, 0x40 + rng.randrange(0x34) if rng.random() < 0.6 else rng.randrange(256), 1),
                       (t + 0x6, rng.randrange(16) if rng.random() < 0.5 else rng.randrange(256), 1),
                       (t + 0x11, rng.randrange(256), 1)]
    high = rng.randint(-1, 49)
    fields.append((REVEAL_COUNT, high & 0xFFFFFFFF, 4))
    full = rng.random() < 0.3
    fields += [(REVEAL_POINTS + i * 16, 1 if full or rng.random() < 0.85 else 0, 1) for i in range(50)]
    return fields


def spell_x(rng, pool):
    return rng.randint(-10, 100) & 0xFFFFFFFF


SPELL_HANDLERS = {address: (count, "thiscall", "probe") for address, count in [
    (0x00548340, 4), (0x00548fb0, 3), (0x00549530, 3), (0x00549650, 3), (0x00549700, 3), (0x005497f0, 3),
    (0x005498a0, 3), (0x00549f40, 4), (0x0054a370, 3), (0x0054a7c0, 4), (0x0054a540, 4), (0x0054a1e0, 3),
    (0x00549bd0, 3), (0x005499b0, 3), (0x0054aa40, 4), (0x0054acd0, 4), (0x005492a0, 3), (0x005495d0, 1)]}


def spell_spec(name, address, **kwargs):
    return Spec(name, address, "thiscall", [below(4), spell_x, spell_x], eax_mask=0, presets=spell_area,
                max_cases=300, **kwargs)


SPECS += [
    spell_spec("CGameData_SpellRevealSpot", 0x00549530),
    spell_spec("CGameData_SpellBurnTrees", 0x00549650),
    # Exempt: 0x54978e (the signed-modulo fix-up of (Random & 0xffff) % 4):
    # dead, the value is never negative.
    spell_spec("CGameData_SpellAddFish", 0x00549700, exempt_blocks=(0x54978e,)),
    spell_spec("CGameData_SpellHalveResources", 0x005497f0),
    spell_spec("CGameData_SpellGrowTrees", 0x005498a0),
    Spec("CGameData_SpellRevealMap", 0x005495d0, "thiscall", [below(4)], eax_mask=0,
         presets=lambda rng, pool, args: spell_area(rng, pool, args + [0, 0]),
         globals=lambda rng, pool, args: [(0x007dfd84, rng.choice([0, 1, 1, 2]), 4)],
         callees={0x004febe0: (1, "thiscall", "probe")}, max_cases=300),
    Spec("CGameData_ExecuteSpell", 0x005480b0, "thiscall",
         [anything, lambda rng, pool: rng.randint(-50, 800) & 0xFFFFFFFF,
          lambda rng, pool: rng.randint(-50, 800) & 0xFFFFFFFF, anything, below(34, 0.1)], eax_mask=0,
         presets=lambda rng, pool, args: [(SIGHT_MAP, rng.randint(1, 768), 4)], callees=SPELL_HANDLERS),
]


# --- package S2: spells on settlers and goods (spell_units.cpp, spell_goods.cpp)
# Small maps (24..48) whose tiles are all pinned, settlers / piles 1..60,
# players 0..7 (teams, races, mana; now and then a race > 3, which the goods
# spells assert on: a throw, compared), a ring table of 1001 offsets
# reaching 1 + n // 30 tiles, the map effect table (the padding of the make
# functions' stack temporaries ignored, as for SpawnSpellEffects) and some
# free timed-event records. StealGoodFromPile and DropGoodNear (verified;
# they need consistent carriers and piles) are probes; a steal takes one
# unit when it reports success. The C runtime's rand (0x61f802: the effect
# delays and scatter of ConvertGoods/GodlyGoods) is a probe too: it keeps
# its state in the thread data (TLS); it also returns negative values, which
# cover the signed-modulo fix-ups.
S2_SETTLERS = 60
S2_PILES = 60
S2_TYPES = [5, 8, 15, 30, 31, 32, 33, 34, 35, 22, 23, 36, 40, 40, 22, 0, 1, 0x2b]
S2_JOBS = [0, 0x35, 0x37, 0x8e, 0xb0, 0xd4, 0xd5, 0x12e, 0x12f, 0x37, 0x35, 3, 0x10, 0xad]
S2_GOODS = [11, 8, 2, 10, 28, 1, 3, 24, 0xff]
S2_RING = 1001
S2_PADDING = tuple(WORLD_EFFECTS + slot * 0x4c + p for slot in range(1, 200) for p in EFFECT_PADDING)


def s2_world(rng, goods: bool):
    size = rng.randint(24, 48)
    fields = [(SIGHT_MAP, size, 4), (LOCAL_PLAYER, rng.randrange(4), 4)]
    for n in range(S2_RING):
        d = 1 + n // 30
        fields += [(NEIGHBOUR_OFFSETS + n * 16, rng.randint(-d, d) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + n * 16 + 4, rng.randint(-d, d) & 0xFFFFFFFF, 4)]
    for x in range(size):
        for y in range(size):
            t = tile(x, y)
            if goods:
                fields += [(t + 0x2, 1 + rng.randrange(S2_PILES) if rng.random() < 0.5 else 0, 2),
                           (t + 0xa, 0x20 if rng.random() < 0.2 else rng.randrange(0x20), 1),
                           (t + 0x8, 0xff if rng.random() < 0.15 else rng.randrange(8), 1)]
            else:
                fields += [(t, 1 + rng.randrange(S2_SETTLERS) if rng.random() < 0.4 else 0, 2),
                           (t + 0xa, rng.randrange(256), 1),
                           (t + 0x10, 0 if rng.random() < 0.25 else 1 + rng.randrange(5), 1)]
    for n in range(S2_SETTLERS + 1):
        s = SETTLERS + n * SETTLER_BYTES
        fields += [(s + 0x4, rng.randrange(size), 2), (s + 0x6, rng.randrange(size), 2)]
        if not goods:
            fields += [(s, rng.randrange(8), 1), (s + 0x1, rng.choice(S2_TYPES), 1), (s + 0x2, rng.randrange(5), 1),
                       (s + 0xe, rng.choice(S2_JOBS), 2),
                       (s + 0x10, 0 if rng.random() < 0.25 else rng.randrange(S2_SETTLERS + 1), 2),
                       (s + 0x12, 0 if rng.random() < 0.25 else rng.randrange(S2_SETTLERS + 1), 2),
                       (s + 0x1c, 0xFFFF if rng.random() < 0.3 else rng.getrandbits(32), 4),
                       (s + 0x24, rng.randrange(size), 2), (s + 0x26, rng.randrange(size), 2)]
    if goods:
        for n in range(S2_PILES + 1):
            p = PILES + n * PILE_BYTES
            fields += [(p, rng.choice(S2_GOODS), 1), (p + 1, 0 if rng.random() < 0.15 else 1 + rng.randrange(8), 1),
                       (p + 6, rng.randrange(size), 2), (p + 8, rng.randrange(size), 2)]
    for player in range(8):
        record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
        race = rng.randrange(4) if not goods or rng.random() < 0.95 else rng.choice([4, 0xFFFFFFFF])
        fields += [(record + 0x4, race, 4), (record + 0xc, rng.randrange(4), 4), (record + 0x8c, rng.randrange(100), 4)]
        fields += [(record + 0x94 + slot * 4, rng.randrange(60), 4) for slot in range(9)]
    for record in range(1, 12):
        fields.append((0x36700 + record * 0x14, 0 if rng.random() < 0.4 else 1 + rng.randrange(0xFFFF), 2))
    fields += [(WORLD_EFFECTS + offset, value, width) for offset, value, width in effect_table(0.0)(rng, None, None)]
    return fields


def s2_unit_presets(rng, pool, args):
    return s2_world(rng, goods=False)


def s2_goods_presets(rng, pool, args):
    return s2_world(rng, goods=True)


def s2_globals(rng, pool, args):
    return [(0x007dfd48, rng.getrandbits(32), 4), (0x007dfdb0, rng.choice([0, 67, 67, 3]), 4)]


def s2_steal(machine, ecx, args, h):
    """A successful steal (AL != 0) takes one unit from the pile."""
    if h & 0xFF:
        p = PILES + (args[0] & 0xFFFF) * PILE_BYTES + 1
        amount = int.from_bytes(machine.guest_read(WORLD_BASE + p, 1), "little")
        _write(machine, p, amount - 1, 1)


S2_GOODS_CALLEES = {
    0x00505c20: (1, "thiscall", lambda h: h & 0xFFFFFF01, s2_steal),   # StealGoodFromPile
    0x005046b0: (4, "thiscall", "probe"),                               # DropGoodNear
    0x0061f802: (0, "cdecl", lambda h: (h & 0x7FFF) if h & 0x10000 else (h >> 32) & 0xFFFFFFFF),  # rand
}


def s2_coordinate(rng, pool):
    return rng.randint(-6, 54) & 0xFFFFFFFF


def s2_inner(rng, pool):
    """ConvertGoods' centre: its 200 offsets (7 tiles) stay on a 24-tile map
    (the original loops forever on an area tile off the map)."""
    return rng.randint(8, 15)


def s2_spec(name, address, args, goods, **kwargs):
    return Spec(name, address, "thiscall", args, eax_mask=0,
                presets=s2_goods_presets if goods else s2_unit_presets, globals=s2_globals,
                ignore_bytes=S2_PADDING, callees=S2_GOODS_CALLEES if goods else None, max_cases=300, **kwargs)


SPECS += [
    Spec("CGameData_IsSpellTarget", 0x0054af60, "thiscall", [below(S2_SETTLERS + 1)], presets=s2_unit_presets,
         max_cases=400),
    Spec("CGameData_ReleaseSpellTarget", 0x0054b000, "thiscall", [below(S2_SETTLERS + 1)], presets=s2_unit_presets,
         max_cases=600),
    # Exempt: 0x54a00a (a spell outside 10, 12, 16, 17, 24, 25): dead, the
    # only caller ExecuteSpell passes just these six.
    s2_spec("CGameData_SpellMarkSoldiers", 0x00549f40,
            [below(8), s2_coordinate, s2_coordinate, lambda rng, pool: rng.choice([10, 12, 16, 17, 24, 25])],
            goods=False, exempt_blocks=(0x54a00a,)),
    s2_spec("CGameData_SpellConvertSettlers", 0x0054a370, [below(8), s2_coordinate, s2_coordinate], goods=False),
    s2_spec("CGameData_SpellTeleportEnemies", 0x0054a7c0,
            [below(8), s2_coordinate, s2_coordinate, below(S2_SETTLERS + 1)], goods=False),
    s2_spec("CGameData_SpellGatherUnits", 0x0054a540,
            [below(8), s2_coordinate, s2_coordinate, below(S2_SETTLERS + 1)], goods=False),
    s2_spec("CGameData_SpellDisarmBowmen", 0x0054a1e0, [below(8), s2_coordinate, s2_coordinate], goods=False),
    s2_spec("CGameData_SpellConvertGoods", 0x00549bd0, [below(8), s2_inner, s2_inner], goods=True),
    # Exempt: 0x549a5c (the signed-modulo fix-up of Random() & 0xffff):
    # dead, the value is never negative; 0x549abe (an amount above 8 is
    # capped): dead with the image's table, amount <= (max + 1) / 2 - 1 +
    # max / 2 <= max and every max in 0x65a47c is at most 8.
    s2_spec("CGameData_SpellGodlyGoods", 0x005499b0, [below(8), s2_coordinate, s2_coordinate], goods=True,
            exempt_blocks=(0x549a5c, 0x549abe)),
    s2_spec("CGameData_SpellGoodsBan", 0x0054aa40,
            [below(8), s2_coordinate, s2_coordinate, below(S2_SETTLERS + 1)], goods=True),
    s2_spec("CGameData_SpellGoodsSummon", 0x0054acd0,
            [below(8), s2_coordinate, s2_coordinate, below(S2_SETTLERS + 1)], goods=True),
]


# WalkSettler 0x500860 (package A): one step of a walking settler. The
# window around the settler (and around a soldier's post, and radius 15
# for settlers looking for a ship) holds small settler/building numbers
# whose records are set up near the settler; everything else is lazily
# random. FindPath is replaced for the case by a stub written over its
# first bytes (WALK_FIND_PATH_CODE): it logs its argument (compared like
# any write) and returns the case's results in call order, so that every
# combination of the two searches of a ship can be chosen (FindPath itself
# has its own spec). The other callees with effects beyond simple fields
# are probes (all verified): ResetSettlerToIdle, RemoveSettlerFromLists
# and the map effects walk lists, SetSettlerAnimation needs the graphics
# object, MoveSettlerToBuildingTileA uses the building's coordinates,
# AllocTable36700Record scans 10000 records.
WALK_JOBS = (0x002, 0x003, 0x006, 0x00a, 0x00c, 0x00f, 0x011, 0x01c, 0x01d, 0x02a, 0x02b, 0x02d, 0x030,
             0x035, 0x04c, 0x04e, 0x068, 0x090, 0x093, 0x098, 0x09c, 0x0ab, 0x0b0, 0x0b2, 0x0b5, 0x0bb,
             0x0bc, 0x0e5, 0x0e8, 0x0ed, 0x0f1, 0x0f6, 0x0f9, 0x0fe, 0x102, 0x133, 0x135, 0x140, 0x141,
             0x146, 0x147, 0x148, 0x149, 0x14a, 0x165, 0x168, 0x16a, 0x16d, 0x001, 0x037, 0x0ac, 0x004)
WALK_TYPES = (0x29, 0x2c, 0x2b, 0x17, 0x28, 0x16, 0x05, 0x08, 0x0f, 0x24, 0x00, 0x01, 0x20, 0x13)
WALK_LAND_TYPES = (0x05, 0x08, 0x0f, 0x20, 0x1e, 0x1f, 0x23, 0x21, 0x22, 0x16, 0x28, 0x17, 0x24)
WALK_TERRAINS = (0x10, 0x30, 0x05, 0x06, 0x20, 0x21, 0x2f, 0x00, 0x12, 0x1c, 0x40, 0x08)
WALK_OTHERS = 30  # settlers 1..30 and buildings 1..30 are the ones the windows refer to
WALK_FIND_PATH = 0x004a1ee0
WALK_PATH_INDEX = 0x004a1f40
WALK_PATH_LOG = 0x004a1f44      # 4 dwords: FindPath's arguments
WALK_PATH_RESULTS = 0x004a1f60  # 4 dwords
WALK_FIND_PATH_CODE = (
    b"\xa1" + struct.pack("<I", WALK_PATH_INDEX)              # mov eax, [index]
    + b"\x8b\x54\x24\x04"                                       # mov edx, [esp+4]
    + b"\x89\x14\x85" + struct.pack("<I", WALK_PATH_LOG)      # mov [log+eax*4], edx
    + b"\xff\x05" + struct.pack("<I", WALK_PATH_INDEX)         # inc dword [index]
    + b"\x8b\x04\x85" + struct.pack("<I", WALK_PATH_RESULTS)  # mov eax, [results+eax*4]
    + b"\xc2\x04\x00")                                          # ret 4
WALK_PROBES = {0x0050cf50: (1, "thiscall", "probe"), 0x00545b90: (10, "thiscall", "probe"),
               0x00544e70: (7, "thiscall", "probe"), 0x004fefd0: (10, "thiscall", "probe"),
               0x0050dff0: (1, "thiscall", "probe"), 0x0050de90: (1, "thiscall", "probe"),
               0x0050e2c0: (2, "thiscall", "probe")}
_WALK_RINGS: dict = {}


def walk_rings(rings: int) -> list:
    """The offset table (world+0x110a588: dx, dy, distance) for rings
    0..rings-1, then an entry with distance 1000 that ends every search."""
    if rings not in _WALK_RINGS:
        fields, k = [], 0
        for r in range(rings):
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    if hex_distance(dx, dy) == r:
                        record = NEIGHBOUR_OFFSETS + k * 16
                        fields += [(record, dx & 0xFFFFFFFF, 4), (record + 4, dy & 0xFFFFFFFF, 4), (record + 8, r, 4)]
                        k += 1
        fields.append((NEIGHBOUR_OFFSETS + k * 16 + 8, 1000, 4))
        _WALK_RINGS[rings] = fields
    return _WALK_RINGS[rings]


def walk_path_result(rng) -> int:
    pick = rng.randrange(11)
    if pick < 3:
        return rng.randrange(6)
    if pick == 10:
        return 0x10 | rng.randrange(6)
    return (rng.randrange(8), 8 | rng.randrange(8), 0x10 | rng.randrange(8), 0xff, 8 + rng.randrange(8),
            rng.randrange(0x20), rng.getrandbits(9))[pick - 3]


def walk_globals(rng, pool, args):
    first = walk_path_result(rng)
    second = first if rng.random() < 0.25 else walk_path_result(rng)
    if rng.random() < 0.3:
        # A ship's two searches near each other: the second one in the
        # first's direction or next to it (or straight back).
        c = rng.randrange(6)
        first = rng.choice((c, c | 8))
        second = rng.choice((c, (c + 5) % 6, (c + 1) % 6, (c + 3) % 6))
    fields = [(WALK_FIND_PATH + i, b, 1) for i, b in enumerate(WALK_FIND_PATH_CODE)]
    fields += [(WALK_PATH_INDEX, 0, 4), (WALK_PATH_RESULTS, first, 4), (WALK_PATH_RESULTS + 4, second, 4),
               (WALK_PATH_RESULTS + 8, rng.getrandbits(32), 4), (WALK_PATH_RESULTS + 12, rng.getrandbits(32), 4)]
    return fields


def walk_other(rng, k, x, y, owner):
    """Settler k (1..30) near (x, y), often of the walker's player."""
    record = SETTLERS + k * SETTLER_BYTES
    art = rng.choice(WALK_TYPES + (0x2c, 0x2c, 0x29, 0x17, 0x17, 0x17))
    job = rng.choice((0xae, 0xae, 0x145, 0xab, 0xac, 0x01, 0x37, 0x01, 0x37, 0x35)) if rng.random() < 0.7 \
        else rng.choice(WALK_JOBS)
    return [(record, owner if rng.random() < 0.5 else rng.randrange(4), 1), (record + 0x1, art, 1),
            (record + 0x2, rng.randrange(4), 1), (record + 0x4, x + rng.randint(-15, 15), 2), (record + 0x6, y + rng.randint(-15, 15), 2),
            (record + 0xe, job, 2), (record + 0x15, rng.randrange(8), 1),
            (record + 0x20, rng.randrange(1, WALK_OTHERS + 1), 2)]


def walk_state(rng, pool, args):
    n = args[0]
    record = SETTLERS + n * SETTLER_BYTES
    scenario = rng.choice(("walk", "walk", "walk", "walk", "arrive", "arrive", "arrive", "arrive", "shoot",
                           "shoot", "board", "board", "tail"))
    x, y = rng.randint(40, 340), rng.randint(40, 340)
    fields = [(MAP_SIZE, 384, 4)] + walk_rings(16 if scenario == "board" else 8)
    races = [rng.randrange(4) if rng.random() < 0.85 else rng.randint(4, 6) for _ in range(4)]
    for p in range(4):
        fields += [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, races[p], 4),
                   (PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, rng.randrange(3), 4)]
    owner = rng.randrange(4)
    if scenario == "walk":
        pick = rng.random()
        art = 0x29 if pick < 0.2 else rng.choice((0x2c, 0x2b)) if pick < 0.5 else rng.choice(WALK_TYPES)
        job = rng.choice(WALK_JOBS)
        if art in (0x2b, 0x2c) and rng.random() < 0.5:
            job = rng.choice((0x147, 0x148, 0x149))
    elif scenario in ("shoot", "board"):
        art = rng.choice(WALK_LAND_TYPES) if rng.random() < 0.8 else rng.choice(WALK_TYPES)
        job = rng.choice((0x35, 0xb0))
    elif scenario == "tail":
        art = rng.choice((0x28, 0x16, 0x17))
        job = rng.choice((0x35, 0xb0))
    else:
        art = rng.choice(WALK_TYPES)
        job = rng.choice(WALK_JOBS + (0x35, 0xb0, 0xab, 0xab, 0xab, 0xab, 0x135, 0x148, 0x149, 0x14a))
    cache = [0xFFFF if rng.random() < 0.5 else rng.randint(30, 350) for _ in range(6)]
    if rng.random() < 0.5:
        cache[0], cache[1] = rng.randint(30, 350), rng.randint(30, 350)
    cache[4] = rng.choice((0xFFFD, 0xFFFE, cache[4]))
    post = (x + rng.randint(-20, 20), y + rng.randint(-20, 20))
    cache[2], cache[3], cache[5] = post[0], post[1], rng.choice((40, 40, rng.randrange(20)))
    state = rng.randrange(7)
    if scenario == "shoot":
        state, cache[4] = 2, 0xFFFD
    elif scenario == "board":
        state = 1
    if scenario == "walk":
        tx, ty = x + rng.randint(-10, 10), y + rng.randint(-10, 10)
        if (tx, ty) == (x, y):
            tx += 1
        if rng.random() < 0.05:
            tx = rng.choice((384, 385, 0xFFFF))
        if rng.random() < 0.3:
            cache[4], cache[5] = 0xFFFD, rng.randrange(10)
    else:
        tx, ty = (x, y) if rng.random() < 0.85 else (x + rng.randint(-3, 3), y + rng.randint(-3, 3))
    count = rng.choice((0xff, 0xf0 + rng.randrange(16), 6 + rng.randrange(2) + 8 * rng.randrange(4),
                        rng.getrandbits(8)))
    fields += [(record, owner, 1), (record + 0x1, art, 1), (record + 0x2, rng.randrange(4), 1),
               (record + 0x3, rng.randrange(8), 1), (record + 0x4, x, 2), (record + 0x6, y, 2),
               (record + 0xe, job, 2), (record + 0x10, rng.randrange(WALK_OTHERS + 1), 2),
               (record + 0x12, 0 if rng.random() < 0.5 else rng.randrange(WALK_OTHERS + 1), 2),
               (record + 0x15, 0 if rng.random() < 0.25 else rng.randrange(8), 1),
               (record + 0x16, 0 if rng.random() < 0.4 else rng.getrandbits(8), 1),
               (record + 0x19, 0 if rng.random() < 0.4 else rng.getrandbits(8), 1),
               (record + 0x1a, state, 1),
               (record + 0x1c, 0xFFFF if rng.random() < 0.5 else rng.randrange(6), 4),
               (record + 0x20, rng.randrange(1, WALK_OTHERS + 1), 2),
               (record + 0x24, tx, 2), (record + 0x26, ty, 2), (record + 0x3c, count, 1)]
    fields += [(record + 0x30 + 2 * k, cache[k], 2) for k in range(6)]
    region = rng.randint(1, 3)
    here = tile(x, y)
    area = rng.choice((0, 0x8000 | rng.getrandbits(15), rng.randint(1, 0xfff), rng.randint(1, 0xfff)))
    # The six neighbours (step, swap partner) and the ring around them
    # (seven-tile units).
    for dx in range(-2, 3):
        for dy in range(-2, 3):
            if hex_distance(dx, dy) > 2 or (dx, dy) == (0, 0):
                continue
            t = tile(x + dx, y + dy)
            fields += [(t, 0 if rng.random() < (0.5 if hex_distance(dx, dy) == 1 else 0.75) else
                        rng.randrange(1, WALK_OTHERS + 1), 2),
                       (t + 0x2, 0 if rng.random() < 0.85 else rng.getrandbits(16), 2),
                       (t + 0x6, rng.choice(WALK_TERRAINS), 1)]
            if hex_distance(dx, dy) == 1:
                fields += [(t + 0x8, rng.choice((0xff, owner, rng.randrange(4))), 1),
                           (t + 0x7, 0 if rng.random() < (0.97 if scenario == "tail" else 0.75)
                            else rng.getrandbits(8), 1),
                           (t + 0x14, rng.getrandbits(16), 2)]
    for k in range(1, WALK_OTHERS + 1):
        fields += walk_other(rng, k, x, y, owner)
    if scenario == "shoot":
        px, py = post
        for b in range(1, WALK_OTHERS + 1):
            r = BUILDINGS + b * BUILDING_BYTES
            fields += [(r, rng.randrange(4), 1), (r + 0x2, px + rng.randint(-15, 15), 2),
                       (r + 0x4, py + rng.randint(-15, 15), 2),
                       (r + 0x6, rng.choice((0xf, 0x10, 0x12, rng.randrange(0x40))), 1)]
        for dx in range(-5, 6):
            for dy in range(-5, 6):
                if hex_distance(dx, dy) > 5:
                    continue
                t = tile(px + dx, py + dy)
                flags = rng.getrandbits(8) & 0xdf
                if rng.random() < 0.4 or (races[owner] == 3 and (dx, dy) == (0, 0)):
                    # A building; race 3 soldiers always see one first (see
                    # the known difference in settler_walk.cpp).
                    flags |= 0x20
                fields += [(t + 0xa, flags, 1), (t + 0x2, rng.randrange(1, WALK_OTHERS + 1), 2),
                           (t, 0 if rng.random() < 0.6 else rng.randrange(1, WALK_OTHERS + 1), 2)]
    if scenario == "board":
        for dx in range(-15, 16):
            for dy in range(-15, 16):
                if hex_distance(dx, dy) < 15 and hex_distance(dx, dy) > 2:
                    fields.append((tile(x + dx, y + dy), 0 if rng.random() < 0.9 else
                                   rng.randrange(1, WALK_OTHERS + 1), 2))
        # A few ships with shore tiles in reach (sometimes the settler's own tile).
        for _ in range(rng.randint(1, 3)):
            k = rng.randrange(1, WALK_OTHERS + 1)
            side, ring = rng.randrange(6), rng.randint(1, 8)
            if rng.random() < 0.5:
                bx, by = x - DIR_X[side] * ring, y - DIR_Y[side] * ring  # the settler stands on the shore
            else:
                bx, by = x + rng.randint(-10, 10), y + rng.randint(-10, 10)
            r = SETTLERS + k * SETTLER_BYTES
            fields += [(r, rng.choice((owner, rng.randrange(4))), 1), (r + 0x1, 0x2c, 1), (r + 0x4, bx, 2),
                       (r + 0x6, by, 2), (r + 0xe, 0x145 if rng.random() < 0.1 else 0x35, 2),
                       (r + 0x15, rng.randrange(8), 1), (tile(bx, by) + 0x6, rng.choice((5, 6, 6, 0x10)), 1)]
            if 0 < hex_distance(bx - x, by - y) < 15:
                fields.append((tile(bx, by), k, 2))
            sx, sy = bx + DIR_X[side] * ring, by + DIR_Y[side] * ring
            fields += [(tile(sx, sy) + 0x6, 0x30, 1),
                       (tile(sx, sy) + 0x10, region if rng.random() < 0.8 else rng.randint(1, 3), 1)]
    # The settler's own tile last (the windows above may cover it).
    fields += [(here, n if rng.random() < 0.85 else rng.randrange(WALK_OTHERS + 1), 2), (here + 0x14, area, 2),
               (here + 0x10, region, 1),
               (here + 0x6, 0x30 if scenario == "board" and rng.random() < 0.5 else
                rng.choice((0x20, 0x21, 0x2f)) if scenario == "tail" and rng.random() < 0.7 else
                rng.choice(WALK_TERRAINS), 1),
               (here + 0x8, rng.choice((0xff, owner, rng.randrange(4))), 1),
               (here + 0x7, 0 if rng.random() < 0.75 else rng.getrandbits(8), 1),
               (here + 0xa, rng.getrandbits(8) & rng.choice((0xff, 0xf2, 0xf0)), 1),
               (AREAS + (area & 0xfff) * AREA_BYTES, rng.choice((owner, rng.randrange(5))), 1)]
    return fields


SPECS += [
    # 0x502444/0x50244e (type 0x29), 0x5030d7/0x5030e1 and 0x50323a/
    # 0x503244 (ships): dead. The step time's special case for type 0x17
    # is inlined into every step, but these paths only run for types 0x29
    # and 0x2b/0x2c.
    Spec("CGameData_WalkSettler", 0x00500860, "thiscall", [between(WALK_OTHERS + 1, 400)], presets=walk_state,
         globals=walk_globals, callees=WALK_PROBES, max_cases=4000,
         exempt_blocks=(0x502444, 0x50244e, 0x5030d7, 0x5030e1, 0x50323a, 0x503244)),
]


# UpdateEconomy 0x4e05e0 (was "UpdatePhase2"; update_economy.cpp): one call
# per case at a chosen phase (counter +0x110a57c). Every callee that walks
# lists, floods or changes settlers is probed (calls and arguments
# compared), with small return values (settler / building numbers) and a
# few side effects that the function reads back (TakeBuildingExtraTiles
# marks the extra tiles taken, TakeGoodFromPile may change the pile's
# good, DispatchPileCarrier reserves a unit); GoodsRequestPriority,
# TileDistance and IsOnMap run. The world gets the bounded lists the
# function walks itself: garrison chains, the offset table, trade rings of
# six piles, the area pile and carrier lists, and the priority ranks
# +0x1f6 as a permutation (so every rank is found).
ECON_COUNTER = 0x110a57c
ECON_AREA_COUNT = 0xe115dc
ECON_PRESENCE_MAX = 0xde7444
ECON_OFFSETS = 0x110a588
ECON_TRADES = 0x10f45e8
ECON_TRADE_BYTES = 0x5a


def _small(n, zero=0.5):
    return lambda h: 0 if (h & 0xFF) < int(256 * zero) else 1 + (h >> 8) % n


def _write(machine, offset, value, size):
    machine.guest_write(WORLD_BASE + offset, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))


def _econ_extra_tiles(machine, ecx, args, h):
    if h & 1:
        _write(machine, BUILDINGS + args[0] * BUILDING_BYTES + 0x20, 0xFE, 1)


def _econ_take_good(machine, ecx, args, h):
    if h & 1:
        _write(machine, PILES + (args[0] & 0xFFFF) * PILE_BYTES, (h >> 8) % 36, 1)


def _econ_dispatch(machine, ecx, args, h):
    if h & 1:
        p = PILES + (args[0] & 0xFFFF) * PILE_BYTES + 2
        leaving = int.from_bytes(machine.guest_read(WORLD_BASE + p, 1), "little")
        _write(machine, p, leaving + 1, 1)


ECONOMY_UPDATE_PROBES = {
    0x005104c0: (0, "thiscall", "probe"),                 # RecountPlayerPopulation
    0x004dacd0: (2, "thiscall", _small(60)),              # RequestSettlerForBuilding
    0x005199e0: (2, "thiscall", lambda h: int((h & 0x7F) != 0)),  # IsFreeOfCatapultsNearby
    0x0051b6f0: (2, "thiscall", "probe"),                 # BlockTile
    0x004ddf30: (4, "thiscall", "probe"),                 # AddBuildingGoodsRequest
    0x004db500: (3, "thiscall", _small(60, 0.8)),         # SendNearestWorkerToBuilding
    0x004fec10: (4, "thiscall", "probe"),                 # NotifyLocalPlayer
    0x0050cc10: (1, "thiscall", "probe"),                 # LeaveBuildingSlot
    0x00510440: (1, "thiscall", "probe"),                 # ClearSettlerJob
    0x0050e200: (1, "thiscall", "probe"),                 # SetTowerSoldierAnimation
    0x0050e750: (1, "thiscall", lambda h: h % 60),        # CountAreaSettlersOfClass0
    0x0050e820: (2, "thiscall", lambda h: h % 40),        # CountAreaSettlersOfType
    0x0050c9e0: (4, "thiscall", _small(60, 0.3)),         # CreateSettler
    0x0050e2c0: (2, "thiscall", "probe"),                 # SetSettlerAnimation
    0x004dd6f0: (1, "thiscall", "probe", _econ_extra_tiles),  # TakeBuildingExtraTiles
    0x004ddfe0: (2, "thiscall", "probe"),                 # SetAreaBuildingSlotA
    0x004de040: (2, "thiscall", "probe"),                 # SetAreaBuildingSlotB
    0x004ddd70: (1, "thiscall", "probe"),                 # SendNearestSettlerToBuilding
    0x004ddbd0: (3, "thiscall", "probe"),                 # SendNearestSettlerToPile
    0x00505820: (1, "thiscall", "probe", _econ_take_good),  # TakeGoodFromPile
    0x004ffb40: (3, "thiscall", "probe"),                 # SubtractPlayerCounter
    0x005058c0: (1, "thiscall", "probe"),                 # PileIncAmountLeaving
    0x00505bc0: (1, "thiscall", "probe"),                 # PileIncAmountComing
    0x005052c0: (1, "thiscall", "probe", _econ_dispatch),  # DispatchPileCarrier
    0x004de0a0: (3, "thiscall", "probe"),                 # FetchGoodsToTradeBuilding
    0x004df150: (2, "thiscall", _small(60, 0.45)),        # BuildingInputWord2A
    0x004df220: (2, "thiscall", lambda h: h % 31),        # BuildingInputWord24
    0x00503610: (3, "thiscall", "probe"),                 # SetSettlerTarget
}

ECON_SOLDIERS = (5, 0x20, 0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22)
ECON_PRESENCE_TYPES = (5, 8, 0xf, 0x16, 0x17, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29,
                       0x2b, 0x2c)
_LEVELLING_OK: list = []


def _levelling_sites() -> list:
    """(race, type) pairs whose levelling list in S3.EXE 0x6aa170 has all
    three end markers (the others would make the original read past it)."""
    if not _LEVELLING_OK:
        import os
        import struct

        import pefile
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "Settlers3Ultimate", "S3.EXE")
        pe = pefile.PE(path, fast_load=True)
        data = pe.get_data(0x6aa170 - 0x400000, 0x960 * 4 * 0x39)
        for race in range(4):
            for art in range(0x39):
                entry = struct.unpack("<600i", data[(race * 0x39 + art) * 0x960:(race * 0x39 + art + 1) * 0x960])
                if -0x80000000 in entry and -0x7fffffff in entry and -0x7ffffffd in entry:
                    _LEVELLING_OK.append((race, art))
    return _LEVELLING_OK


def _econ_pile(rng, fields, number, good=None, x=None, y=None):
    p = PILES + number * PILE_BYTES
    amount = rng.randrange(9)
    fields += [(p, good if good is not None else (0xFF if rng.random() < 0.25 else rng.randint(1, 35)), 1),
               (p + 1, amount, 1),
               (p + 2, amount if rng.random() < 0.25 else rng.randrange(amount + 1) if rng.random() < 0.8
                else rng.randrange(256), 1),
               (p + 3, rng.randrange(9 - amount) if rng.random() < 0.9 else rng.randrange(9), 1)]
    if x is not None:
        fields += [(p + 6, x, 2), (p + 8, y, 2)]


def _econ_building(rng, fields, number, phase):
    b = BUILDINGS + number * BUILDING_BYTES
    owner = 0xFF if rng.random() < 0.08 else rng.randrange(20)
    site = rng.random() < 0.3
    race, art = rng.choice(_levelling_sites()) if site else (rng.randrange(4), rng.randint(1, 0x39))
    if not site:
        art = rng.randint(1, 0x39)
    if not site and rng.random() < 0.3:
        art = rng.choice((0xf, 0x10, 0x12, 0x21, 0x22, 0x13, 0xc, 0xd, 0x35, 0x1f, 0x2f, 0x30, 0x32, 0x20,
                          0x1a, 0x29, 0x2a, 0x2e, 0x36))
    x, y = rng.randint(20, 360), rng.randint(20, 360)
    dx, dy = x + rng.randrange(-2, 3), y + rng.randrange(-2, 3)
    worker = 0 if rng.random() < 0.35 else rng.randint(1, 60)
    fields += [(b, owner, 1), (b + 2, x, 2), (b + 4, y, 2), (b + 6, art, 1), (b + 7, race, 1),
               (b + 0xA, dx, 2), (b + 0xC, dy, 2),
               (b + 0x21, (rng.choice((1, 1, 2, 2, 3, rng.randrange(256)))) if site else 0, 1),
               (b + 0x18, rng.getrandbits(8), 1)]
    # Recruiting building 0x13 (UpdateBuilding13): for three of four x
    # positions its recruit counters +0x19..+0x1b stay below 3 and its
    # area's counters +0x242.. are non-zero (below), so the branch that
    # counts an area counter down after a successful request (0x4e265e,
    # 0x4e26d7, 0x4e274a) is reached within 2,000 cases. Derived from values
    # already drawn: the random stream, and so every other case, is unchanged.
    recruit = art == 0x13 and x % 4 != 0
    for k in range(0x16, 0x21):
        if k != 0x18:
            v = (rng.randrange(9) if rng.random() < 0.6 else
                 rng.choice((0, 0xFF, 0xFE, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23, 0x24, 0x30,
                             0x33, 0x40, 0x43, 1, 2, 0xFA)))
            fields.append((b + k, v % 3 if recruit and k in (0x19, 0x1A, 0x1B) else v, 1))
    fields += [(b + 0x30, rng.randrange(0x50), 2), (b + 0x32, rng.getrandbits(16), 2),
               (b + 0x22, 0xFFFF if rng.random() < 0.15 else rng.randrange(6), 2)]
    # Water buildings and the shipyard: states that fix their wanted inputs.
    states = {0x1f: (0x10, 0x20), 0x2f: (0x11, 0x21), 0x30: (0x12, 0x22), 0x32: (0x13, 0x23),
              0x20: (0x30, 0x31, 0x33, 0x34, 0x40, 0x43, 0x44, 0x2f)}.get(art)
    if states and rng.random() < 0.7:
        fields += [(b + 0x1F, rng.choice(states), 1), (b + 0x20, 0xFF if rng.random() < 0.7 else rng.randrange(9), 1)]
    hp = {0xf: 250, 0x10: 500, 0x12: 1000}.get(art, 250)
    fields.append((b + 0x38, hp - rng.randrange(2) if rng.random() < 0.7 else rng.getrandbits(16), 2))
    # Input piles, output / ring pile.
    piles = [rng.randint(1, 300) for _ in range(4)]
    for k, p in enumerate(piles):
        fields.append((b + 0x10 + 2 * k if k < 3 else b + 0xE, p, 2))
        _econ_pile(rng, fields, p)
    # Trade buildings: a ring of six piles and a trade record.
    if art in (0x21, 0x22):
        ring = [piles[3]] + [rng.randint(301, 600) for _ in range(5)]
        for k, p in enumerate(ring):
            _econ_pile(rng, fields, p, good=0xFF if rng.random() < 0.35 else rng.randint(1, 35))
            fields.append((PILES + p * PILE_BYTES + 0xC, ring[(k + 1) % 6], 2))
        trade = rng.randint(1, 20)
        t = ECON_TRADES + trade * ECON_TRADE_BYTES
        fields += [(b + 0x34, trade, 2), (t + 1, rng.choice((0, 1, 2, 2, 1, 3)), 1),
                   (t + 8, 0xFFFF if rng.random() < 0.1 else rng.randrange(384), 2)]
        for g in range(1, 0x24):
            offered = 0 if rng.random() < 0.3 else rng.randrange(4)
            fields += [(t + 0x12 + g, offered, 1), (t + 0x36 + g, offered if rng.random() < 0.3 else rng.randrange(4), 1)]
    # Military buildings: a garrison chain.
    if art in (0xf, 0x10, 0x12) and not site:
        # Settlers of its own (chains of two buildings must not share any).
        members = rng.sample(range(100 + 8 * number, 108 + 8 * number), rng.randrange(6))
        worker = members[0] if members else 0
        if rng.random() < 0.3:  # nobody coming either: an empty building warns its player
            fields += [(b + k, 0, 1) for k in (0x19, 0x1A, 0x1B, 0x1C, 0x1F, 0x20)]
        for k, s in enumerate(members):
            r = SETTLERS + s * SETTLER_BYTES
            fields += [(r + 1, rng.choice(ECON_SOLDIERS) if rng.random() < 0.85 else rng.randrange(0x30), 1),
                       (r + 0x10, members[k + 1] if k + 1 < len(members) else 0, 2)]
    fields.append((b + 8, worker, 2))
    # The door tile and its neighbours (piles dropped there, the door's settler).
    for ox, oy in ((0, 0), (-1, 0), (1, 0), (-1, -1), (1, 1), (0, -1), (0, 1)):
        p = rng.randint(1, 300) if rng.random() < 0.8 else 0
        fields.append((tile(dx + ox, dy + oy) + 2, p, 2))
        if p:
            _econ_pile(rng, fields, p)
    fields.append((tile(dx, dy), 0 if rng.random() < 0.6 else rng.randint(1, 60), 2))
    # An area for the door tile (barracks-like 0x13).
    area = rng.randint(1, 8)
    a = AREAS + area * AREA_BYTES
    fields += [(tile(dx, dy) + 0x14, area, 2), (a + 0x23E, rng.randrange(120), 1), (a + 0x241, rng.randrange(2), 1)]
    for k in range(3):
        v = rng.choice((0, 0, 1, 2))
        fields.append((a + 0x242 + k, max(v, 1) if recruit else v, 1))


def _econ_area(rng, fields, area, first_used):
    """`first_used`: no used area came before it in this call; its player is
    then one of 0..18 (races 0..3; for a race above 3 the original's highest
    rank would be a stale stack slot)."""
    a = AREAS + area * AREA_BYTES
    player = 0xFF if rng.random() < 0.15 else rng.randrange(19 if first_used else 20)
    fields.append((a, player, 1))
    ranks = list(range(1, 36))
    rng.shuffle(ranks)
    for g in range(1, 36):
        fields += [(a + 0x1F6 + g, ranks[g - 1], 1),
                   (a + 0xE2 + 2 * g, 0 if rng.random() < 0.8 else rng.randint(1, 60), 2),
                   (a + 0x9A + 2 * g, 0 if rng.random() < 0.8 else rng.randint(1, 60), 2),
                   (a + 0x52 + 2 * g, 0 if rng.random() < 0.3 else rng.randint(1, 60), 2)]
        # The goods' pile list: up to four piles near (200, 200); distances
        # are never multiples of 5, so no distance score is 0 (the original
        # divides by it).
        members = rng.sample(range(1000 + 300 * area + 8 * g, 1008 + 300 * area + 8 * g), rng.randrange(5))
        for k, p in enumerate(members):
            d = rng.choice((1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14))
            _econ_pile(rng, fields, p, good=g, x=200 + d * rng.choice((-1, 1)), y=200)
            fields += [(PILES + p * PILE_BYTES + 4, 0xFF if rng.random() < 0.5 else rng.choice((0, 0xFE, 0xFD, 1)), 1),
                       (PILES + p * PILE_BYTES + 0xC, members[k + 1] if k + 1 < len(members) else 0, 2)]
        fields.append((a + 0xA + 2 * g, members[0] if members else 0, 2))
    for t in range(0x33):
        fields += [(a + 0x245 + t, 0 if rng.random() < 0.2 else rng.randint(1, 255), 1),
                   (a + 0x278 + 2 * t, rng.randrange(300), 2)]
    # Idle carriers.
    carriers = rng.sample(range(1, 61), rng.randrange(6))
    for k, s in enumerate(carriers):
        r = SETTLERS + s * SETTLER_BYTES
        fields += [(r + 0x12, carriers[k - 1] if k else 0, 2),
                   (r + 0x10, carriers[k + 1] if k + 1 < len(carriers) else 0, 2),
                   (r + 0x14, 0x80 if rng.random() < 0.2 else rng.randrange(0x80), 1),
                   (r + 4, rng.randint(150, 250), 2), (r + 6, rng.randint(150, 250), 2)]
    fields.append((a + 0x12A, carriers[0] if carriers else 0, 2))
    return first_used and player == 0xFF


def economy_update_world(rng, pool, args):
    r = rng.random()
    phase = 0 if r < 0.12 else 17 if r < 0.35 else rng.randint(1, 16)
    fields = [(ECON_COUNTER, phase, 4), (MAP_SIZE, 384, 4),
              (ECON_PRESENCE_MAX, rng.choice((0, 1, 3, 19)), 4)]
    for p in range(20):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xC, rng.randrange(20), 4))
    if phase == 0:
        fields[1] = (MAP_SIZE, rng.choice((0, 15, 16, 17, 33, 48, 64, rng.randrange(70))), 4)
        fields.append((ECON_AREA_COUNT, rng.randrange(6), 4))
        fields += [(SETTLER_COUNT, rng.randrange(40), 4), (BUILDING_COUNT, rng.randrange(40), 4)]
    elif phase == 17:
        count = rng.randrange(5)
        fields.append((ECON_AREA_COUNT, count, 4))
        # Races 0..3, only player 19 (never the first used area's) may have 4..7.
        for p in range(20):
            fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4,
                           rng.randrange(4, 8) if p == 19 and rng.random() < 0.5 else rng.randrange(4), 4))
        first_used = True
        for area in range(1, count + 1):
            first_used = _econ_area(rng, fields, area, first_used)
        # Request-list buildings (BuildingInputWord2A returns 1..60): all at
        # (200, 200), sites or finished consumers.
        consumers = (0, 0xb, 0xa, 0xd, 0xc, 0x16, 0x28, 0x20, 0x1f, 0x2f, 0x30, 0x27, 0x4, 0x1d, 0x11, 0x17,
                     0x31, 0x26, 0x14, 0x7, 0x9, 0x8, 0x25, 0x32, 0x38, 0x34)
        for n in range(1, 61):
            b = BUILDINGS + n * BUILDING_BYTES
            site = rng.random() < 0.3
            fields += [(b + 2, 200, 2), (b + 4, 200, 2), (b + 0x21, rng.randint(1, 3) if site else 0, 1),
                       (b + 6, rng.choice(consumers) if rng.random() < 0.8 else rng.randrange(0x39), 1)]
            for k in range(3):
                fields.append((b + 0x10 + 2 * k, 3000 + n * 3 + k, 2))
                fields.append((PILES + (3000 + n * 3 + k) * PILE_BYTES, rng.randint(1, 35), 1))
    else:
        settlers = phase + 16 * rng.randrange(4) + rng.randrange(16) if rng.random() < 0.9 else rng.randrange(phase)
        buildings = phase + 16 * rng.randrange(6) + rng.randrange(16) if rng.random() < 0.9 else rng.randrange(phase)
        fields += [(SETTLER_COUNT, settlers, 4), (BUILDING_COUNT, buildings, 4)]
        for n in range(phase, settlers + 1, 16):
            s = SETTLERS + n * SETTLER_BYTES
            fields += [(s, 0xFF if rng.random() < 0.15 else rng.randrange(20), 1),
                       (s + 1, rng.choice(ECON_PRESENCE_TYPES) if rng.random() < 0.7 else rng.randrange(0x30), 1),
                       (s + 4, rng.randrange(400), 2), (s + 6, rng.randrange(400), 2)]
        # The offset table: a few rings within distance < 10.
        count = rng.randrange(14)
        for k in range(count + 1):
            o = ECON_OFFSETS + 16 * k
            fields += [(o, rng.randrange(-3, 4) & 0xFFFFFFFF, 4), (o + 4, rng.randrange(-3, 4) & 0xFFFFFFFF, 4),
                       (o + 8, (rng.randrange(-2, 10) if k < count else rng.randint(10, 20)) & 0xFFFFFFFF, 4)]
        for n in range(phase, buildings + 1, 16):
            _econ_building(rng, fields, n, phase)
    return fields


SPECS += [
    # The code ends at 0x4e492b; the switch tables follow it.
    Spec("CGameData_UpdateEconomy", 0x004e05e0, "thiscall", [], presets=economy_update_world,
         callees=ECONOMY_UPDATE_PROBES, max_cases=4000, cover_until=0x004e492c,
         # 0x4e45f3 (a candidate building 0): dead, the candidates are the
         # non-zero numbers of the request list. 0x4e0c63 (skip a 0x80000002
         # in a site's object-clear list): dead for the game's table. The
         # original reads that list in pairs from the block list's end
         # marker 0x80000001 on, one dword off the lists' layout, and no
         # 0x80000002 of S3.EXE 0x6aa170 (races 0..3, types 1..0x38) lands
         # on a pair start (checked with a script that follows the
         # original's parse; the markers act as small offsets through the
         # 32-bit wrap of the tile offset).
         exempt_blocks=(0x004e0c63, 0x004e45f3)),
]


# UpdateSettlers 0x4e9670 (update_settlers.cpp, settler_jobs_*.cpp): one
# call per case on settlers 1..1-3 with jobs of one handler family (the
# letters of US_FAMILY, the same table as kJobFamily). Every callee with
# effects is probed (calls and arguments compared), with return values as
# the in-process harness's StubReturn (settler_jobs_test.h); the cheap
# queries run (Random, RandomBelow6, NeighbourX/Y, Next/PreviousDirection,
# TileDistance, IsOnMap, TileScreenX/Y, SettlerStartHealth, NoOp_ret4).
# Settlers, buildings and piles 1..US_OTHERS have coordinates on the map;
# the tiles around them hold small settler/object numbers and areas.
US_FAMILY = ("uuuuububbbububbububbbbbsbbbbuubb" "bbbbbbbbbbuububbummmmumub-mmmmmm"
             "mmb-mmmmmmmmububbbmmmmmmmmmmmmmm" "mmmmmbbbubbmffffffffmffffffffmmm"
             "mmmffffff-bbbsufuffuffffufffufff" "fffffffffbbuuuumufuffufffffuufff"
             "ffffffffffffffffffffddmmmmmmmmmm" "mmddduddudddduddduddddudduddddud"
             "dduddd-dddddd-dddddd-dddddd-dddd" "ddddddddmmmmmmddsffususmmmssssss"
             "uusssssuuuusssssssss-sssssssssss" "ss-ssussusussussus----------s-ss"
             "ssssssmmmmmmmmmmsssss")
assert len(US_FAMILY) == 0x195
US_JOBS = {f: [j for j, c in enumerate(US_FAMILY) if c == f] for f in sorted(set(US_FAMILY))}
US_OTHERS = 40   # settlers, buildings 1..40
US_PILES = 80    # piles 1..80
US_AREAS = 64


def _us_walk(h):
    return 1 if (h & 3) == 0 else 0


US_PROBES = {
    0x004d97b0: (2, "thiscall", "probe"),   # DestroyBuildingAt
    0x004dbc50: (1, "thiscall", "probe"),   # BuildingDone
    0x004dda10: (1, "thiscall", "probe"),   # ReleaseBuildingExtraTiles
    0x004df490: (1, "thiscall", "probe"),   # ClearBuildingFlag02
    0x004df510: (1, "thiscall", "probe"),   # ClearBuildingFlag04
    0x004df590: (1, "thiscall", "probe"),   # ClearBuildingFlag08
    0x004df610: (1, "thiscall", "probe"),   # ClearBuildingFlag10
    0x004df9b0: (2, "thiscall", lambda h: h & 1),  # BuildingPileHasGoods
    0x004e8ba0: (3, "thiscall", "probe"),   # QueuePendingEvent
    0x004f9a00: (1, "thiscall", lambda h: h & 1),  # MinerJobEnter
    0x004f9b80: (1, "thiscall", lambda h: h & 1),  # MinerJobFood
    0x004fa220: (1, "thiscall", lambda h: h & 1),  # MinerJobDig
    0x004faa70: (2, "thiscall", lambda h: h % 12),  # TileSlope
    0x004fec10: (4, "thiscall", "probe"),   # NotifyLocalPlayer
    0x004fefd0: (10, "thiscall", "probe"),  # AllocTable36700Record
    0x004ff3c0: (1, "thiscall", "probe"),   # ClearAreaGoodsSettings1D2
    0x004ff510: (3, "thiscall", "probe"),   # AddStatistic
    0x004ffb00: (3, "thiscall", "probe"),   # AddPlayerCounter
    0x004ffb40: (3, "thiscall", "probe"),   # SubtractPlayerCounter
    0x00500860: (1, "thiscall", _us_walk),  # WalkSettler: run again now and then
    0x00503610: (3, "thiscall", "probe"),   # SetSettlerTarget
    0x00503760: (4, "thiscall", lambda h: h & 1),  # CanSettlerEnterTile
    0x005046b0: (4, "thiscall", "probe"),   # DropGoodNear
    0x00505650: (1, "thiscall", "probe"),   # AddGoodsToPile
    0x00505820: (1, "thiscall", "probe"),   # TakeGoodFromPile
    0x005058c0: (1, "thiscall", "probe"),   # PileIncAmountLeaving
    0x00505bc0: (1, "thiscall", "probe"),   # PileIncAmountComing
    0x00505c20: (1, "thiscall", lambda h: h & 1),  # StealGoodFromPile
    0x0050c9e0: (4, "thiscall", _small(US_OTHERS, 0.25)),  # CreateSettler
    0x0050cb20: (5, "thiscall", "probe"),   # SpawnSettlersAround
    0x0050cc10: (1, "thiscall", "probe"),   # LeaveBuildingSlot
    0x0050ccb0: (1, "thiscall", "probe"),   # EnterBuildingSlot
    0x0050cf50: (1, "thiscall", "probe"),   # ResetSettlerToIdle
    0x0050dd20: (4, "thiscall", _small(US_OTHERS, 0.25)),  # CreateSevenTileUnit
    0x0050de90: (1, "thiscall", "probe"),   # MoveSettlerToBuildingTileA
    0x0050df40: (1, "thiscall", "probe"),   # MoveSettlerToBuildingTileB
    0x0050e140: (1, "thiscall", "probe"),   # SetSoldierAnimationInBuilding
    0x0050e2c0: (2, "thiscall", "probe"),   # SetSettlerAnimation (needs the graphics object)
    0x0050e320: (2, "thiscall", "probe"),   # SetSettlerSavedAnimation (graphics object)
    0x0050e380: (1, "thiscall", "probe"),   # RestoreSettlerAnimation
    0x0050e750: (1, "thiscall", lambda h: h % 8),  # CountAreaSettlersOfClass0
    0x0050e820: (2, "thiscall", lambda h: h % 8),  # CountAreaSettlersOfType
    0x0050e9f0: (1, "thiscall", lambda h: h & 1),  # UpdateSoldier
    0x00510440: (1, "thiscall", "probe"),   # ClearSettlerJob
    0x00510770: (2, "thiscall", "probe"),   # AddPileClaim
    0x00510800: (2, "thiscall", lambda h: h & 1),  # CountOtherPileClaims
    0x005199e0: (2, "thiscall", lambda h: h & 1),  # IsFreeOfCatapultsNearby
    0x00519670: (2, "thiscall", "probe"),   # UpdateShadeAround
    0x00519a90: (2, "thiscall", "probe"),   # RecalculateTerritory
    0x0051a460: (2, "thiscall", "probe"),   # UpdateBorderFlagsAround
    0x0051b5d0: (3, "thiscall", "probe"),   # SetOwnerOfFreeTile
    0x0051b640: (2, "thiscall", "probe"),   # ClearTileOwner
    0x0051b6f0: (2, "thiscall", "probe"),   # BlockTile
    0x0051b770: (2, "thiscall", "probe"),   # UnblockTile
    0x0051b7f0: (2, "thiscall", "probe"),   # WearPath
    0x0051ba00: (2, "thiscall", "probe"),   # MakeFieldTile
    0x00544630: (8, "thiscall", "probe"),   # MapEffects::Spawn_544630
    0x005449f0: (8, "thiscall", "probe"),   # MapEffects::Spawn_5449f0
    0x00544dc0: (7, "thiscall", "probe"),   # MapEffects::Spawn_544dc0
    0x00545120: (5, "thiscall", "probe"),   # MapEffects::Spawn_545120
}


def _us_tiles(rng, fields, x, y, radius=1):
    """Small settler / object numbers and areas around (x, y)."""
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            if hex_distance(dx, dy) > radius:
                continue
            t = tile(x + dx, y + dy)
            fields += [(t, 0 if rng.random() < 0.6 else rng.randint(1, US_OTHERS), 2),
                       (t + 0x2, rng.randrange(US_OTHERS + 1), 2),
                       (t + 0x14, rng.randrange(US_AREAS), 2)]


def _us_settler(rng, fields, n, family):
    s = SETTLERS + n * SETTLER_BYTES
    x, y = rng.randint(40, 340), rng.randint(40, 340)
    job = rng.choice(US_JOBS[family])
    art = rng.randrange(0x30)
    fields += [(s, 0xFF if rng.random() < 0.05 else rng.randrange(4), 1), (s + 0x1, art, 1),
               (s + 0x2, rng.randrange(4), 1), (s + 0x4, x, 2), (s + 0x6, y, 2),
               (s + 0xc, 0 if rng.random() < 0.9 else rng.randint(1, 3), 2), (s + 0xe, job, 2),
               (s + 0x10, rng.randrange(US_OTHERS + 1), 2), (s + 0x12, rng.randrange(US_OTHERS + 1), 2),
               (s + 0x14, (0x80 | rng.getrandbits(8)) if rng.random() < 0.1 else rng.getrandbits(7), 1),
               (s + 0x20, rng.randrange(US_OTHERS + 1), 2), (s + 0x22, rng.randrange(US_OTHERS + 1), 2),
               (s + 0x24, x + rng.randint(-10, 10), 2), (s + 0x26, y + rng.randint(-10, 10), 2),
               (s + 0x28, x + rng.randint(-10, 10), 2), (s + 0x2a, y + rng.randint(-10, 10), 2)]
    _us_tiles(rng, fields, x, y)
    return {"x": x, "y": y, "job": job, "art": art, "record": s}


def _us_building(rng, fields, b):
    r = BUILDINGS + b * BUILDING_BYTES
    x, y = rng.randint(40, 340), rng.randint(40, 340)
    dx, dy = x + rng.randint(-2, 2), y + rng.randint(-2, 2)
    fields += [(r, 0xFF if rng.random() < 0.05 else rng.randrange(4), 1), (r + 0x2, x, 2), (r + 0x4, y, 2),
               (r + 0x6, rng.randint(1, 0x38), 1), (r + 0x7, rng.randrange(4), 1),
               (r + 0x8, rng.randrange(US_OTHERS + 1), 2), (r + 0xa, dx, 2), (r + 0xc, dy, 2),
               (r + 0xe, rng.randint(1, US_PILES), 2), (r + 0x10, rng.randint(1, US_PILES), 2),
               (r + 0x12, rng.randint(1, US_PILES), 2), (r + 0x14, rng.randint(1, US_PILES), 2),
               (r + 0x30, x + rng.randint(-5, 5), 2), (r + 0x32, y + rng.randint(-5, 5), 2),
               (r + 0x34, rng.randint(1, 20), 2)]
    _us_tiles(rng, fields, dx, dy)


def _us_pile(rng, fields, p):
    r = PILES + p * PILE_BYTES
    fields += [(r, rng.choice((0, 0xFF, rng.randint(1, 35))), 1), (r + 1, rng.randrange(9), 1),
               (r + 6, rng.randint(40, 340), 2), (r + 8, rng.randint(40, 340), 2),
               (r + 0xa, rng.randrange(US_PILES + 1), 2), (r + 0xc, rng.randrange(US_PILES + 1), 2)]


def _us_clear_job(machine, ecx, args, h):
    """ClearSettlerJob's effect: the settler idles (job 0x39, counter and
    building 0), so handlers that clear a job and run again end."""
    r = SETTLERS + args[0] * SETTLER_BYTES
    _write(machine, r + 0xe, 0x39, 2)
    _write(machine, r + 0xc, 0, 2)
    _write(machine, r + 0x22, 0, 2)


def _us_set_owner(machine, ecx, args, h):
    """SetOwnerOfFreeTile (x, y, owner): the tile gets the owner (fills end)."""
    _write(machine, tile(args[0] & 0xFFFF, args[1] & 0xFFFF) + 8, args[2], 1)


def _us_clear_owner(machine, ecx, args, h):
    """ClearTileOwner (x, y): the tile has no owner any more."""
    _write(machine, tile(args[0] & 0xFFFF, args[1] & 0xFFFF) + 8, 0xFF, 1)


US_PROBES[0x00510440] = (1, "thiscall", "probe", _us_clear_job)
US_PROBES[0x0051b5d0] = (3, "thiscall", "probe", _us_set_owner)
US_PROBES[0x0051b640] = (2, "thiscall", "probe", _us_clear_owner)


# Per family: more set-up for settler n of a case (after the generic part
# of _us_settler; `ctx` holds the case's choices: "x", "y", "job", "art",
# "building", ...), and the family's exempt blocks (dead code etc., each
# with its reason). One function per family so that they can be extended
# independently.


# ---- family u (units, settler_jobs_units.cpp) ----
# Scenarios per handler of 0x4e971c..0x4eb43b (and the loop's own blocks):
# job 0 (lists), 0x8e, 0x37 (thief, ships steering, geologist / pioneer
# searches, soldiers, crowded / blocked tiles), 0xae (enemies in reach or a
# new guard), 0xad, 0xac (searches of the player's areas), 1 (foreign
# ground, surplus carriers, crowded tiles, the area search), 0xb0, 0x35,
# the walking jobs, job 4 for every type and building, 0x170, and settlers
# the loop skips (job above 0x194).
US_EXEMPT_U = (
    # "Digger trouble" (0x4f922f..): DebugLog, then `jmp $` (an endless loop on
    # purpose); our version logs and sleeps forever. Never run in a test.
    0x004f922f, 0x004f9263,
)

US_U_SHIP_TYPES = (0x2b, 0x2c)
US_U_ARRIVE_BUILDINGS = (7, 8, 9, 29, 37, 56, 10, 11, 12, 13, 31, 47, 48, 50, 23, 49, 1, 2, 6, 57, 60, 0xff)


def _us_u_sector(dx, dy):
    """A direction 0..5 for an offset (only consistency matters)."""
    if dx == 0 and dy == 0:
        return 0
    if dy == 0:
        return 0 if dx > 0 else 3
    if dx == dy:
        return 1 if dx > 0 else 4
    if dx == 0:
        return 2 if dy > 0 else 5
    if dx > 0:
        return 0 if dx > dy else 1
    return 3 if dx < dy else 4


_US_U_SECTORS: list = []


def _us_u_sectors():
    """MapOffset::sector for the 217 ring entries of walk_rings(9) (same order;
    the ship steering reads entries 1..216, and the game's table only holds
    sectors 0..5)."""
    if not _US_U_SECTORS:
        k = 0
        for r in range(9):
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    if hex_distance(dx, dy) == r:
                        _US_U_SECTORS.append((NEIGHBOUR_OFFSETS + k * 16 + 12, _us_u_sector(dx, dy), 4))
                        k += 1
    return _US_U_SECTORS


def _us_u_ring(x, y, radius):
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            if hex_distance(dx, dy) <= radius:
                yield x + dx, y + dy, hex_distance(dx, dy)


# The neighbours in the order the crowded-side check looks at them.
US_U_SIDES = ((-1, 0), (-1, -1), (0, -1), (1, 0), (1, 1), (0, 1))


def _us_u_neighbours(rng, fields, x, y, crowded):
    """The six neighbours: no settlers, or (crowded) the first occupied one
    in check order is a random side, later ones random."""
    first = rng.randrange(6)
    for k, (dx, dy) in enumerate(US_U_SIDES):
        t = tile(x + dx, y + dy)
        busy = crowded and (k == first or (k > first and rng.random() < 0.4))
        fields.append((t, rng.randint(1, US_OTHERS) if busy else 0, 2))


def _us_family_u(rng, fields, n, ctx):
    s = ctx["record"]
    x, y = ctx["x"], ctx["y"]
    if rng.random() < 0.1:
        # Next to the map edge (neighbours off the map).
        x = rng.choice((0, 383))
        fields.append((s + 0x4, x, 2))
    here = tile(x, y)
    fields.append((s + 0x8, rng.choice((5, 6, rng.randrange(7))), 4))  # facing direction (5 and up wrap to 0)
    owner = rng.randrange(4)
    fields += walk_rings(9) + _us_u_sectors()
    for p in range(4):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, rng.randrange(2), 4))
    for a in range(US_AREAS):
        fields.append((AREAS + a * AREA_BYTES, rng.choice((0xff, rng.randrange(4))), 1))
    fields.append((s, owner, 1))
    scenario = rng.choice(("new", "new", "fight", "idle", "idle", "idle", "idle", "idle", "idle", "guard",
                           "guard", "heal", "seek", "seek", "seek", "nothing", "nothing", "nothing", "nothing",
                           "nothing", "count", "unitwalk", "walk", "arrive", "arrive", "arrive", "arrive", "arrive",
                           "arrive", "arrive", "arrive", "bee", "skip"))
    area = rng.randint(1, US_AREAS - 1)
    region = rng.randint(1, 3)
    if scenario == "skip":
        fields.append((s + 0xe, rng.choice((0x195, 0x1ff, 0xffff)), 2))
        return
    if scenario == "new":
        art = rng.choice((0, 1, 3, 9, 5, 8, 0xf, 0x16, 0x28, 0x2c, 0x2b, 0x17, rng.randrange(0x30)))
        fields += [(s + 0xe, 0, 2), (s + 0x1, art, 1), (here + 0x14, area, 2)]
        head = 0 if rng.random() < 0.4 else rng.randint(1, US_OTHERS)
        fields += [(PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES + 0x20 + 2 * art, head, 2),
                   (AREAS + area * AREA_BYTES + 0x12a + 2 * art, head, 2)]
        return
    if scenario == "fight":
        fields.append((s + 0xe, 0x8e, 2))
        return
    if scenario == "heal":
        fields += [(s + 0xe, 0xad, 2), (s + 0x3, rng.choice((0, 1, 50, 99, 100, 200)), 1)]
        return
    if scenario == "count":
        fields += [(s + 0xe, 0xb0, 2), (s + 0x3c, rng.choice((0, 1, 2, 5)), 1)]
        return
    if scenario == "unitwalk":
        fields += [(s + 0xe, 0x35, 2), (s + 0x1, rng.choice((5, 8, 0xf, 0, 0x2c)), 1)]
        return
    if scenario == "walk":
        fields.append((s + 0xe, rng.choice([j for j in US_JOBS["u"] if j not in (0, 1, 4, 0x35, 0x37, 0x8e, 0xac,
                                                                              0xad, 0xae, 0xb0, 0x170)]), 2))
        return
    if scenario == "bee":
        fields.append((s + 0xe, 0x170, 2))
        return
    if scenario == "arrive":
        art = (rng.randrange(0x30) if rng.random() < 0.6 else rng.choice((9, 0xa, 0xa, 0xa, 0xb, 0xb, 0xb, 0x1c, 0x1c))
               if rng.random() < 0.95 else rng.randint(0x30, 0xff))
        b = rng.randint(1, US_OTHERS)
        fields += [(s + 0xe, 4, 2), (s + 0x1, art, 1), (s + 0x22, b, 2),
                   (BUILDINGS + b * BUILDING_BYTES + 6, rng.choice(US_U_ARRIVE_BUILDINGS), 1),
                   (here + 0x14, area, 2), (AREAS + area * AREA_BYTES + 0x23e, rng.randrange(120), 1)]
        return
    if scenario == "guard":
        art = rng.choice((5, 0x20, 8, 0x1e, 0xf, 0x22, 0x16, 0))
        fields += [(s + 0xe, 0xae, 2), (s + 0x1, art, 1)]
        enemy = rng.random() < 0.6
        for tx, ty, d in _us_u_ring(x, y, 7):
            if d == 0:
                continue
            k = 0
            if rng.random() < (0.08 if enemy else 0.02):
                k = rng.randint(1, US_OTHERS)
                r = SETTLERS + k * SETTLER_BYTES
                fields += [(r, rng.randrange(4), 1), (r + 1, rng.choice((5, 0xf, 8, 0x22, 0, 0x20)), 1)]
            fields.append((tile(tx, ty), k, 2))
        if rng.random() < 0.5:
            # The created guard (CreateSettler returns 0..40): its own record.
            pass
        return
    if scenario == "seek":
        civilian = rng.random() < 0.85
        fields += [(s + 0xe, 0xac, 2), (s + 0x1, rng.choice((0, 1, 3, 9)) if civilian else 5, 1),
                   (here + 0x10, region, 1), (here + 0x14, area, 2)]
        mode = rng.choice(("own", "other", "none", "none_free"))
        for tx, ty, d in _us_u_ring(x, y, 7):
            if d == 0:
                continue
            t = tile(tx, ty)
            busy = rng.random() < 0.3
            if mode == "own":
                a = area if rng.random() < 0.3 else rng.randint(1, US_AREAS - 1)
            elif mode == "other":
                a = rng.choice((area + 1, area + 2)) % US_AREAS or 1
            else:
                a = 0
            fields += [(t + 0x10, region if rng.random() < 0.9 else region + 1, 1), (t + 0x14, a, 2),
                       (t, rng.randint(1, US_OTHERS) if busy else 0, 2)]
        fields.append((AREAS + area * AREA_BYTES, owner, 1))
        return
    if scenario == "nothing":
        art = 0 if rng.random() < 0.5 else rng.choice((1, 3, 9, 0x10))
        flags = rng.choice((0, 0x80, rng.getrandbits(8)))
        crowd = rng.choice(("free", "free", "free", "crowded", "crowded", "blocked", "blocked_nowhere"))
        fields += [(s + 0xe, 1, 2), (s + 0x1, art, 1), (s + 0x14, flags, 1), (here + 0x14, area, 2),
                   (here + 0x8, owner if rng.random() < 0.6 else rng.randrange(4), 1),
                   (here + 0xa, 0 if not crowd.startswith("blocked") else rng.choice((1, 2, 4, 8)), 1)]
        _us_u_neighbours(rng, fields, x, y, crowd == "crowded")
        player = PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES
        surplus = rng.randrange(4)
        fields += [(player + 0xd4, surplus, 4), (player + 0xd8, rng.choice((surplus, surplus + 1, max(surplus - 1, 0))), 4)]
        if crowd == "blocked_nowhere":
            # No free tile of its area anywhere in the offset table (0x4eabbd).
            for tx, ty, d in _us_u_ring(x, y, 9):
                if d:
                    fields.append((tile(tx, ty) + 0x14, (area + 1) % US_AREAS, 2))
        if crowd == "blocked":
            for tx, ty, d in _us_u_ring(x, y, 4):
                if d == 0:
                    continue
                t = tile(tx, ty)
                fields += [(t + 0x14, area if rng.random() < 0.2 else area + 1, 2),
                           (t + 0xa, rng.choice((0, 0, 1)), 1), (t, rng.choice((0, 0, 1)), 2)]
        return
    # idle (0x37)
    kind = rng.choice(("thief", "ship", "ship", "geologist", "geologist", "pioneer", "pioneer", "soldier",
                       "free", "crowded", "blocked"))
    fields.append((s + 0xe, 0x37, 2))
    if kind == "thief":
        fields += [(s + 0x1, 0x17, 1), (s + 0x15, rng.choice((0, 1, 3)), 1),
                   (here + 0x8, owner if rng.random() < 0.7 else 0xff, 1)]
        _us_u_neighbours(rng, fields, x, y, rng.random() < 0.5)
        return
    if kind == "ship":
        art = rng.choice(US_U_SHIP_TYPES + (0x28, 0x16))
        fields += [(s + 0x1, art, 1), (here + 0x6, rng.choice((5, 5, 6, 0x10)), 1), (here + 0x14, area, 2)]
        if art in (0x28, 0x16):
            fields += [(s + 0x1c, x, 4), (s + 0x20, y, 2), (here + 0x10, region, 1)]
            for tx, ty, d in _us_u_ring(x, y, 3):
                fields += [(tile(tx, ty) + 0x10, region, 1), (tile(tx, ty) + 0xa, 0, 1),
                           (tile(tx, ty) + 0x8, (owner + 1) % 4, 1), (tile(tx, ty) + 0x6, 0x20, 1),
                           (tile(tx, ty) + 0x7, 0, 1)]
        ships = rng.randrange(6)
        for k in range(1, 7):
            fields.append((tile(x + DIR_X[k - 1], y + DIR_Y[k - 1]) + 0x6, rng.choice((5, 0x10, 0x30)), 1))
        for _ in range(ships):
            tx, ty = x + rng.randint(-8, 8), y + rng.randint(-8, 8)
            if hex_distance(tx - x, ty - y) > 8 or (tx, ty) == (x, y):
                continue
            k = rng.randint(1, US_OTHERS)
            t = tile(tx, ty)
            fields += [(t, k, 2), (t + 0x14, area, 2), (t + 0x6, rng.choice((5, 6, 7, 7, 4)), 1),
                       (SETTLERS + k * SETTLER_BYTES + 1, rng.choice(US_U_SHIP_TYPES + (0,)), 1)]
        return
    if kind in ("geologist", "pioneer"):
        geologist = kind == "geologist"
        art = 0x16 if geologist else 0x28
        spot = rng.choice(("here", "near", "none"))
        sx, sy = (x, y) if spot == "here" else (x + rng.randint(-3, 3), y + rng.randint(-3, 3))
        fields += [(s + 0x1, art, 1), (s + 0x1c, 0xffff if spot == "none" else sx, 4),
                   (s + 0x20, 0xffff if spot == "none" else sy, 2), (here + 0x10, region, 1)]
        good = rng.random() < 0.6
        for tx, ty, d in _us_u_ring(sx, sy, 9):
            t = tile(tx, ty)
            ok = good and rng.random() < 0.7
            if geologist:
                fields += [(t + 0x10, region if ok or rng.random() < 0.5 else region + 1, 1),
                           (t + 0xa, 0 if ok or rng.random() < 0.5 else 1, 1),
                           (t + 0x6, 0x20 | rng.randrange(16) if ok or rng.random() < 0.5 else 0x10, 1),
                           (t + 0x7, 0 if ok or rng.random() < 0.5 else 3, 1)]
            else:
                fields += [(t + 0x10, region if ok or rng.random() < 0.5 else region + 1, 1),
                           (t + 0xa, 0 if ok or rng.random() < 0.5 else 0x40, 1),
                           (t + 0x8, rng.choice((0xff, (owner + 1) % 4)) if ok else owner, 1)]
        return
    if kind == "soldier":
        fields.append((s + 0x1, rng.choice((5, 8, 0xf, 0x24, 0x29, 0)), 1))
    else:
        fields.append((s + 0x1, rng.choice((5, 8, 0xf, 0x24, 0x29, 0x25)), 1))
    fields.append((here + 0xa, 0 if kind != "blocked" else rng.choice((1, 4)), 1))
    _us_u_neighbours(rng, fields, x, y, kind == "crowded")
    if kind == "blocked":
        for tx, ty, d in _us_u_ring(x, y, 3):
            if d:
                fields.append((tile(tx, ty) + 0xa, rng.choice((0, 1)), 1))


# ---- family b (building, settler_jobs_building.cpp) ----
US_EXEMPT_B = (
    # "Digger trouble" (0x4f922f): DebugLog, then `jmp $` on purpose; the
    # emulator would only stop at the instruction limit (ours: an endless
    # Sleep loop). Reached only when no tile of the site needs levelling
    # although the site says so.
    0x004f922f, 0x004f9263,
)
_US_B_IMAGE: dict = {}


def _us_b_image() -> dict:
    """The layout lists (S3.EXE 0x6aa170) and building input goods
    (0x6a6824) the building family's presets need."""
    if not _US_B_IMAGE:
        import os
        import struct

        import pefile
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "Settlers3Ultimate", "S3.EXE")
        pe = pefile.PE(path, fast_load=True)
        layouts = pe.get_data(0x6aa170 - 0x400000, 0x960 * 4 * 0x39)
        _US_B_IMAGE["layouts"] = layouts
        _US_B_IMAGE["inputs"] = struct.unpack("<%di" % (0x39 * 3), pe.get_data(0x6a6824 - 0x400000, 0x39 * 12))
    return _US_B_IMAGE


US_B_TYPES = (0, 1, 2, 3, 4, 6, 7, 5, 8, 0xf, 0x20, 0x21, 9)
US_B_SITE_CACHE = 0x1196024
US_B_TRADES = 0x10f45e8


def _us_family_b(rng, fields, n, ctx):
    """The searches of the building family succeed now and then (as the
    in-process test's CustomizeBuilding): small offset-table entries, the
    tiles around the settler's building never blocked (the original hangs
    after "Digger trouble"), a levelling grid, layouts with free spots,
    goods the building wants."""
    import struct
    image = _us_b_image()
    s = ctx["record"]
    x, y = ctx["x"], ctx["y"]
    if rng.random() < 0.7:
        fields.append((s + 0x1, rng.choice(US_B_TYPES), 1))
    job = ctx["job"]
    if rng.random() < 0.55:  # more of the long handlers (diggers, builders, recruits, searches)
        job = rng.choice((5, 5, 5, 5, 5, 7, 0xe, 0xe, 0x12, 0x12, 0x12, 0x12, 0x2e, 0x50, 0x18, 0x18, 0x19, 0x19, 0x1b,
                          0x8a, 0x8a, 0xaa, 0xaa, 0x21, 0x23, 0x23, 0xe, 0xe, 0x69, 0x4f))
        fields.append((s + 0xe, job, 2))
    building = 0 if rng.random() < 0.1 else rng.randint(1, US_OTHERS)  # 0: the site the UI shows (global 0)
    fields.append((s + 0x22, building, 2))
    b = BUILDINGS + building * BUILDING_BYTES
    bx, by = rng.randint(24, 343), rng.randint(24, 343)
    race = rng.randrange(4)
    art = rng.randint(1, 0x38)
    owner = rng.randrange(4)
    pick = rng.randrange(8)
    buildup = 0 if pick < 2 else 1 if pick < 4 else 2 if pick < 5 else 3 if pick < 7 else \
        (50 if rng.random() < 0.25 else rng.randint(4, 51))
    piles = [rng.randint(1, US_PILES) for _ in range(4)]
    levelling = rng.randrange(30)
    fields += [(b, owner if rng.random() < 0.9 else 0xFF, 1), (b + 0x2, bx, 2), (b + 0x4, by, 2), (b + 0x6, art, 1),
               (b + 0x7, race, 1), (b + 0xa, bx + rng.randint(-2, 2), 2), (b + 0xc, by + rng.randint(-2, 2), 2),
               (b + 0x30, bx + rng.randint(-3, 3), 2), (b + 0x32, by + rng.randint(-3, 3), 2),
               (b + 0x22, levelling, 2), (b + 0x18, rng.getrandbits(8), 1), (b + 0x21, buildup, 1),
               (b + 0xe, piles[0], 2), (b + 0x10, piles[1], 2), (b + 0x12, piles[2], 2), (b + 0x14, piles[3], 2),
               (b + 0x8, 0 if rng.random() < 0.25 else rng.randint(1, US_OTHERS), 2)]
    amounts = [rng.randrange(9) for _ in range(4)]
    goods = [rng.choice((0xFF, rng.randint(1, 35))) for _ in range(4)]
    for k in range(4):
        p = PILES + piles[k] * PILE_BYTES
        if k > 0 and rng.random() < 0.25:
            amounts[k] = 0
        if k == 0 and rng.random() < 0.67:
            amounts[k] = rng.randrange(8)
        fields += [(p, goods[k], 1), (p + 1, amounts[k], 1)]
    if rng.random() < 0.5:
        fields += [(b + 0x19, amounts[1], 1), (b + 0x1a, amounts[2], 1)]
    elif rng.random() < 0.2:
        fields += [(b + 0x19, 0, 1), (b + 0x1a, 0, 1)]
    # The settler: near its building, slot, saved counter, tool type, food.
    if rng.random() < 0.5:
        x, y = bx + rng.randint(-12, 12), by + rng.randint(-12, 12)
        fields += [(s + 0x4, x, 2), (s + 0x6, y, 2)]
    slot = rng.randint(1, 3) if rng.random() < 0.67 else rng.getrandbits(8)
    good_pick = rng.randrange(4)
    if rng.random() < 0.33 and buildup == 0:
        slot = goods[1 + good_pick % 3]
    food_pick = rng.randrange(4)
    food = 0 if food_pick == 0 else rng.getrandbits(8) if food_pick == 1 else rng.randrange(12)
    fields += [(s + 0x15, slot & 0xFF, 1), (s + 0x20, rng.randint(1, 3) if rng.random() < 0.8 else rng.randrange(5), 2),
               (s + 0x18, rng.randrange(4), 1), (s + 0x17, food, 1)]
    next_pick = rng.randrange(8)
    if next_pick < 2:
        fields.append((s + 0x10, (2960 if next_pick == 0 else 1460) + rng.randrange(40), 2))
    if good_pick < 3:
        fields.append((s + 0x1a, image["inputs"][art * 3 + good_pick] & 0xFF, 1))
    elif buildup != 0:
        fields.append((s + 0x1a, 1 + rng.randrange(2), 1))
    # Small offset table entries 0..299 (the builder's door search has no limit).
    offsets = [(rng.randint(-4, 4), rng.randint(-4, 4)) for _ in range(300)]
    for k, (ox, oy) in enumerate(offsets):
        o = NEIGHBOUR_OFFSETS + 16 * k
        fields += [(o, ox & 0xFFFFFFFF, 4), (o + 4, oy & 0xFFFFFFFF, 4)]
    # The tiles around the building: never blocked, often one area, a
    # cleared patch with trees or stones.
    same_area = rng.random() < 0.75
    area = rng.randrange(US_AREAS)
    clear = rng.random() < 0.5
    tree_near = rng.random() < 0.5
    tree = 0x44 + rng.randrange(15)
    stone = rng.choice((0x7e, 0x7f)) if rng.random() < 0.35 else 0x73 + rng.randrange(12)
    # Heights near each other, so that the diggers' neighbour tests (equal
    # to the cell's height +-5 / +-7) sometimes hold.
    heights = (100, 100, 105, 107, 93, 95)
    near_heights = rng.random() < 0.5
    # A slope rising by 5 per x: the lower cell's neighbour (x+1, y+1) is 5
    # higher, its others are not 7 / 5 higher (0x4eb5e2).
    slope = ctx["job"] == 5 or job == 5
    slope = slope and rng.random() < 0.3
    for ox in range(-17, 18):
        for oy in range(-17, 18):
            t = tile(bx + ox, by + oy)
            flags = rng.getrandbits(8) & 0xFE
            if clear and -8 <= ox <= 8 and -8 <= oy <= 8:
                flags &= 0xF0
                if (ox + oy) & 3 == 0:
                    fields += [(t + 0x7, 0, 1), (t + 0x2, 0, 2), (t, 0, 2), (t + 0x6, 0x10, 1)]
                elif (ox + oy) & 3 == 1:
                    fields.append((t + 0x7, tree if tree_near else stone, 1))
            height = ((bx + ox) * 5) & 0xFF if slope else rng.choice(heights) if near_heights else rng.getrandbits(8)
            fields += [(t + 0xa, flags, 1), (t + 0x4, height, 1)]
            if same_area:
                fields.append((t + 0x14, area, 2))
    # Where the cutters look; the trade pile under the settler.
    if tree_near:
        fields.append((tile(x - 1, y - 1) + 0x7, tree, 1))
    fields.append((tile(x - 2, y - 2) + 0x7, stone, 1))
    under = rng.randint(1, US_PILES)
    fields.append((tile(x, y) + 0x2, under, 2))
    pr = PILES + under * PILE_BYTES
    if rng.random() < 0.5:
        fields.append((pr + 4, 0xFC, 1))
    pile_building = rng.randint(1, US_OTHERS)
    trade = rng.randint(1, 20)
    fields += [(pr + 0xa, pile_building, 2), (BUILDINGS + pile_building * BUILDING_BYTES + 0x34, trade, 2)]
    if rng.random() < 0.5:
        fields.append((US_B_TRADES + trade * 0x5a + 0x12 + (slot & 0xFF), 0, 1))
    if rng.random() < 0.33:  # sometimes the levelling height equals the tile's
        level = rng.getrandbits(8)
        fields += [(s + 0x17, level, 1), (tile(x, y) + 0x4, level, 1)]
    # Builders at the building stage need a layout with free spots.
    layouts = image["layouts"]
    entry = struct.unpack_from("<600I", layouts, (race * 0x39 + art) * 0x960) if art < 0x39 else ()
    if 0x80000005 in entry and entry.index(0x80000005) < 597:
        m = entry.index(0x80000005)
        for k in range(2):
            dx, dy = entry[m + 1 + 3 * k], entry[m + 2 + 3 * k]
            dx, dy = dx - (1 << 32) if dx >= 1 << 31 else dx, dy - (1 << 32) if dy >= 1 << 31 else dy
            if -20 <= dx < 20 and -20 <= dy < 20:
                t = tile(bx + dx, by + dy)
                # The first spot sometimes taken already (flag 2): the search goes on.
                taken = 2 if k == 0 and rng.random() < 0.4 else 0
                fields.append((t + 0xa, (rng.getrandbits(8) & 0xFC) | taken, 1))
    elif buildup >= 3:
        fields.append((b + 0x21, 1, 1))
    # A digger's levelling grid: a few cells with wanted heights, sometimes empty.
    fields.append((s, owner if rng.random() < 0.95 else 0xFF, 1))
    cache = US_B_SITE_CACHE + owner * 30 * 0x3C2 + levelling * 0x3C2 + 1
    grid = bytearray(31 * 31)
    if rng.random() >= (0.4 if job == 5 else 0.125):  # an empty grid: the site is levelled
        for _ in range(rng.randint(1, 40)):
            grid[rng.randrange(31 * 31)] = 1 if slope else rng.choice((90, 100, 110, rng.randint(1, 255)))
    for k in range(0, 31 * 31, 4):
        chunk = grid[k:k + 4]
        fields.append((cache + k, int.from_bytes(chunk, "little"), len(chunk)))
    # Digger / builder slot bits: one bit (the bit scans 0x4eb725.., 0x4ec62a..).
    fields += [(b + 0x16, 1 << rng.randrange(8) if rng.random() < 0.8 else rng.getrandbits(8), 1),
               (b + 0x17, 1 << rng.choice((0, 0, 0, 1, 2, 3, 4, 5, 6, 7)) if rng.random() < 0.8 else rng.getrandbits(8), 1)]
    # Soldier levels per weapon kind (player +0xb8..+0xc0): 0..3.
    for p in range(4):
        for k in range(3):
            fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xb8 + 4 * k, rng.randrange(4), 4))
    if job in (0x21, 0x23):  # cutting states 1..9 (table 0x4f99d0)
        fields.append((s + 0x17, rng.randrange(10), 1))
    if job == 0x23 and rng.random() < 0.5:  # the stone's last stage (object 0x7e -> 0x7f at x-2, y-2)
        fields += [(tile(x - 2, y - 2) + 0x7, 0x7E, 1), (s + 0x17, 0, 1)]
    if job == 0x69 and rng.random() < 0.5:  # a carrier fetching a digger's tool (+0x18 = 1)
        fields.append((s + 0x18, 1, 1))
    if job == 0x2f:  # planting: the second visit
        fields.append((s + 0x17, rng.randrange(3), 1))
    search_mode = rng.random()  # cutters' and forester's searches: < 0.4 the cursor wraps, < 0.9 found at once
    if job in (0x18, 0x19, 0x1b) and search_mode < 0.4:  # the search cursor wraps (notification)
        fields += [(s + 0x10, (2960 if job != 0x1b else 1460) + rng.randrange(40), 2),
                   (s + 0x18, 0 if rng.random() < 0.5 else 1, 1)]
    if job == 0x1b and 0.4 <= search_mode < 0.9:
        # A free grass spot at the first place the forester looks (0x4edd26).
        k = rng.randrange(300)
        fields += [(b + 0x30, bx, 2), (b + 0x32, by, 2), (s + 0x10, k, 2)]
        tx, ty = bx + offsets[k][0], by + offsets[k][1]
        door_area = rng.randrange(US_AREAS)
        for ox in range(-2, 3):
            for oy in range(-2, 4):
                fields.append((tile(tx + ox, ty + oy) + 0xa, rng.getrandbits(8) & 0xF0, 1))
        t = tile(tx, ty)
        fields += [(t, 0, 2), (t + 0x2, 0, 2), (t + 0x7, 0, 1), (t + 0x6, rng.choice((0x10, 0x11, 0x1c)), 1),
                   (t + 0x14, door_area, 2), (b + 0xa, bx + 1, 2), (b + 0xc, by + 1, 2),
                   (tile(bx + 1, by + 1) + 0x14, door_area, 2)]
    if job == 0x50 and rng.random() < 0.5:  # the site the UI shows (g_selectedBuilding, 0 in the image)
        fields.append((s + 0x22, 0, 2))
    if job == 0x18 and 0.4 <= search_mode < 0.9:
        # A tree at the first place the woodcutter looks (0x4ed14d: found at once).
        k = rng.randrange(300)
        wx, wy = (bx + 0, by + 0)
        fields += [(b + 0x30, wx, 2), (b + 0x32, wy, 2), (s + 0x10, k, 2)]
        tx, ty = wx + offsets[k][0], wy + offsets[k][1]
        door_area = rng.randrange(US_AREAS)
        # The tree at (tx, ty); the area and CanSettlerEnterTile are those of (tx + 1, ty + 1).
        fields += [(tile(tx, ty) + 0x7, 0x44 + rng.randrange(15), 1), (tile(tx + 1, ty + 1) + 0x14, door_area, 2),
                   (b + 0xa, bx + 1, 2), (b + 0xc, by + 1, 2), (tile(bx + 1, by + 1) + 0x14, door_area, 2)]
    if job == 0xe and rng.random() < 0.5:
        # The builder's own tile free, most of its neighbours occupied
        # (flags), so that a free one is found late (0x4ec852: only in the
        # sixth direction tried).
        fields.append((tile(x, y) + 0xa, rng.getrandbits(8) & 0xF0, 1))
        for ox, oy in ((1, 0), (1, 1), (0, 1), (-1, 0), (-1, -1), (0, -1)):
            t = tile(x + ox, y + oy)
            fields.append((t + 0xa, (rng.getrandbits(8) & 0xF0) | (0 if rng.random() < 0.2 else 1), 1))
    if job == 0x2e and rng.random() < 0.7:
        # The forester at a free spot: the tile below it free, its
        # neighbourhood unblocked, grass.
        x, y = rng.randint(40, 340), rng.randint(40, 340)
        fields += [(s + 0x4, x, 2), (s + 0x6, y, 2)]
        for ox in range(-2, 3):
            for oy in range(-2, 4):
                t = tile(x + ox, y + oy)
                fields += [(t + 0xa, rng.getrandbits(8) & 0xF0, 1)]
        t = tile(x, y)
        below = tile(x, y + 1)
        fields += [(t + 0x2, 0, 2), (below, 0, 2), (below + 0x7, 0, 1),
                   (below + 0x6, rng.choice((0x10, 0x11, 0x15, 0x1c, 0x20)), 1)]



# ---- family m (metal, settler_jobs_metal.cpp) ----
US_EXEMPT_M = ()
US_M_TOOLS = (16, 12, 13, 14, 15, 17, 32)  # goods of the toolsmith's seven piles (all of them get set)
US_M_TOOL_TILES = ((0, 0), (-1, 0), (1, 0), (-1, -1), (1, 1), (0, -1), (0, 1))
US_M_RARE_JOBS = (0x80, 0x80, 0x80, 0x74, 0x74, 0xaf, 0xaf, 0x36, 0x129, 0x12b, 0x12d, 0x137, 0x138, 0x139)


def _us_m_toolsmith(rng, fields, b):
    """The door of building b: seven tool piles with few units, an area
    whose tool orders (settings1), shares (settings2) and made counts
    (settings3) reach every branch of the start and the share choice."""
    r = BUILDINGS + b * BUILDING_BYTES
    dx, dy = rng.randint(60, 320), rng.randint(60, 320)
    fields += [(r + 0xa, dx, 2), (r + 0xc, dy, 2)]
    full = rng.random() < 0.15
    for k, (ox, oy) in enumerate(US_M_TOOL_TILES):
        p = rng.randint(1, US_PILES)
        fields += [(tile(dx + ox, dy + oy) + 2, p, 2),
                   (PILES + p * PILE_BYTES + 1, rng.randint(2, 8) if full else rng.choice((0, 0, 0, 1)), 1)]
    area = rng.randint(1, US_AREAS - 1)
    fields.append((tile(dx, dy) + 0x14, area, 2))
    a = AREAS + area * AREA_BYTES
    pick = rng.random()
    # 20%: one tool is the share rule's choice and its made count is 0xff
    # (share 255: (0xff * 256 + 128) / 255 = 256; the others 640 and more).
    chosen = rng.choice(US_M_TOOLS) if rng.random() < 0.2 else None
    for g in US_M_TOOLS:
        ordered = 0 if pick < 0.7 or chosen else rng.choice((0, 0, 0, 1, 2))
        # pick < 0.05: no shares at all (the waits at 0x4ef394 / 0x4efc92).
        share = 0 if pick < 0.05 or rng.random() < (0.9 if pick < 0.1 else 0.3) else rng.randint(1, 6)
        made = rng.choice((0, 1, 2, 5, 9, 0xFF, rng.getrandbits(8)))
        if chosen is not None:
            share, made = (255, 0xFF) if g == chosen else (1, 5)
        fields += [(a + 0x18a + g, ordered, 1), (a + 0x1ae + g, share, 1), (a + 0x1d2 + g, made, 1)]


def _us_m_garrison(rng, fields, b, n):
    """A garrison list (Building::worker, linked by next, increasing
    numbers, ending in 0) of soldiers of all classes and some others."""
    members = sorted(rng.sample(range(10, US_OTHERS + 1), rng.randrange(5)))
    fields.append((BUILDINGS + b * BUILDING_BYTES + 8, members[0] if members else 0, 2))
    for k, s in enumerate(members):
        r = SETTLERS + s * SETTLER_BYTES
        fields += [(r + 1, rng.choice((5, 0x20, 0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22, 0, 0x17)), 1),
                   (r + 0x10, members[k + 1] if k + 1 < len(members) else 0, 2)]


def _us_family_m(rng, fields, n, ctx):
    s = ctx["record"]
    x, y = ctx["x"], ctx["y"]
    job = ctx["job"]
    if rng.random() < 0.5:
        job = rng.choice(US_M_RARE_JOBS)
        fields.append((s + 0xe, job, 2))
    b = rng.randint(1, US_OTHERS)
    r = BUILDINGS + b * BUILDING_BYTES
    fields += [(s + 0x22, b, 2), (r + 0x18, rng.getrandbits(8) | (1 if rng.random() < 0.8 else 0), 1)]
    if rng.random() < 0.6:
        fields.append((tile(x, y), n, 2))  # the settler stands on its own tile
    if job in (0x74, 0x80) or rng.random() < 0.1:
        fields.append((s + 0x1, 0xb, 1))
        _us_m_toolsmith(rng, fields, b)
    elif job == 0xaf:
        _us_m_garrison(rng, fields, b, n)
    elif job == 0x36:
        fields += [(s + 0x1, 0x17 if rng.random() < 0.5 else rng.randrange(0x30), 1),
                   (s + 0x15, rng.choice((0, rng.randint(1, 35))), 1)]
    elif job in (0x129, 0x12b, 0x12d, 0x137, 0x138, 0x139):
        k = (0x129, 0x12b, 0x12d, 0x137, 0x138, 0x139).index(job) % 3
        fields += [(r + 0x20, rng.choice((0, 0, 1, 2, 0xfa, 0xfb, rng.getrandbits(8))), 1),
                   (r + 0x1f, rng.choice((0x10 + k, 0x20 + k, 0x10 + k, 0x20 + k, 0, rng.getrandbits(8))), 1)]
        for w in range(3):
            fields.append((r + 0x1a + w, rng.choice((0, 0, 1, 3)), 1))



# ---- family f (food, settler_jobs_food.cpp) ----
US_EXEMPT_F = ()
US_FOOD_TYPES = (0xB, 0x1C, 0xE, 0x10, 0xC, 0xD, 0x11, 0x12)
US_FOOD_TERRAINS = (0x10, 0x10, 0x10, 0x30, 0x30, 0x00, 0x05, 0x60, 0x60, 0x20, 0x40)
US_FOOD_OBJECTS = (0x55, 0x5A, 0x5B, 0x5C, 0x5D, 0x59, 0x01, 0x80)


def _us_food_patch(rng, fields, cx, cy, radius, area):
    """Tiles around (cx, cy) as the food jobs look for them: mostly free
    grass, shore, water and class 0x60 tiles of the door's area, fields
    (0x55 sown, 0x5a..0x5c ripe, 0x5d harvested) here and there."""
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            if cx + dx < 0 or cy + dy < 0:
                continue
            t = tile(cx + dx, cy + dy)
            fields += [(t + 6, rng.choice(US_FOOD_TERRAINS), 1),
                       (t + 7, 0 if rng.random() < 0.7 else rng.choice(US_FOOD_OBJECTS), 1),
                       (t + 2, 0 if rng.random() < 0.85 else rng.randint(1, US_PILES), 2),
                       (t + 0xA, rng.getrandbits(8) & (0xF6 if rng.random() < 0.85 else 0xFF), 1),
                       (t + 0x14, area if rng.random() < 0.85 else rng.randrange(US_AREAS), 2),
                       (t + 0x11, rng.randrange(8), 1)]


def _us_family_f(rng, fields, n, ctx):
    """Like the in-process FoodCustomize (settler_jobs_food.cpp): the
    settler's own building with its door, work spot, output pile and the
    three weapon piles at the door, area weapon settings (18..20: ordered,
    share, made; zeros and 0xff), tile patches around the work spot and the
    settler."""
    s = ctx["record"]
    b = rng.randint(1, US_OTHERS)
    r = BUILDINGS + b * BUILDING_BYTES
    door_x, door_y = rng.randint(50, 330), rng.randint(50, 330)
    if rng.random() < 0.1:
        # At the map's edge: searches look at spots off the map.
        door_x = rng.choice((2, 3, 4, 379, 380, 381)) if rng.random() < 0.5 else door_x
        door_y = rng.choice((2, 3, 4, 379, 380, 381)) if door_x in (50, 330) or rng.random() < 0.5 else door_y
    work_x, work_y = door_x + rng.randint(-3, 3), door_y + rng.randint(-3, 3)
    x, y = work_x + rng.randint(-4, 4), work_y + rng.randint(-4, 4)
    area = rng.randint(1, US_AREAS - 1)
    art = rng.choice(US_FOOD_TYPES) if rng.random() < 0.7 else ctx["art"]
    if rng.random() < 0.3:
        # The rarer paths: weapon smith start / finish (0x75, 0x86), sowing
        # (0x91), harvesting (0x96), the fisher's and the waterworker's
        # searches (0x76, 0x7c), the pig farm's inputs (0x77; its +0x1d == 5
        # branch 0x4f121f is rare in the mixed run).
        fields.append((s + 0xE, rng.choice((0x75, 0x86, 0x86, 0x91, 0x96, 0x76, 0x7C, 0x7C, 0x77)), 2))
    nxt = rng.randrange(40)
    if rng.random() < 0.1:
        # A search cursor just before its wrap at 2000, with a sane offset
        # table there (the generic rings cover only the first 217 entries).
        nxt = rng.randint(1985, 1999)
        for k in range(1950, 2051):
            o = NEIGHBOUR_OFFSETS + 16 * k
            fields += [(o, rng.randint(-6, 6) & 0xFFFFFFFF, 4), (o + 4, rng.randint(-6, 6) & 0xFFFFFFFF, 4)]
    fields += [(s + 0x1, art, 1), (s + 0x4, x, 2), (s + 0x6, y, 2), (s + 0x22, b, 2),
               (s + 0x10, nxt, 2),
               (s + 0x15, 0 if rng.random() < 0.5 else rng.randrange(30), 1), (s + 0x17, rng.randrange(4), 1),
               (r + 0x18, (rng.getrandbits(8) | 1) if rng.random() < 0.8 else rng.getrandbits(8) & 0xFE, 1),
               (r + 0x1D, rng.randrange(7), 1),
               (r + 0xA, door_x, 2), (r + 0xC, door_y, 2), (r + 0x30, work_x, 2), (r + 0x32, work_y, 2)]
    piles = [rng.randint(1, US_PILES) for _ in range(4)]
    amounts = [rng.randrange(9) if rng.random() < 0.75 else rng.getrandbits(8) for _ in range(4)]
    fields += [(r + 0xE, piles[0], 2), (PILES + piles[0] * PILE_BYTES + 1, amounts[0], 1)]
    a = AREAS + area * AREA_BYTES
    no_weapons = rng.random() < 0.2  # nothing ordered and no shares: the smith waits
    for k in range(3):
        for base in (0x18A, 0x1AE, 0x1D2):
            v = 0 if rng.random() < 0.5 else 0xFF if rng.random() < (0.5 if base == 0x1D2 else 0.3) else rng.randrange(10)
            fields.append((a + base + 18 + k, 0 if no_weapons and base != 0x1D2 else v, 1))
    if rng.random() < 0.15:
        # Nothing ordered, every weapon has a share, some made counts are
        # 0xff: the share path picks any of the three and clears the
        # settings (ClearAreaGoodsSettings1D2) before counting.
        for k in range(3):
            fields += [(a + 0x18A + 18 + k, 0, 1), (a + 0x1AE + 18 + k, rng.randint(1, 9), 1),
                       (a + 0x1D2 + 18 + k, 0xFF if rng.random() < 0.5 else rng.randrange(10), 1)]
    _us_food_patch(rng, fields, work_x, work_y, 7, area)
    _us_food_patch(rng, fields, x, y, 2, area)
    if rng.random() < 0.1:
        # No shore near the work spot: the fisher's search tries all 50
        # offsets in vain.
        for dx in range(-7, 8):
            for dy in range(-7, 8):
                if work_x + dx >= 0 and work_y + dy >= 0:
                    fields.append((tile(work_x + dx, work_y + dy) + 6, 0x10, 1))
    if rng.random() < 0.3:
        # A free grass spot to sow on (job 0x91): the settler's tile and its
        # six neighbours without objects, numbers and blocking flags.
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
            t = tile(x + dx, y + dy)
            fields += [(t + 7, 0, 1), (t + 2, 0, 2), (t + 0xA, rng.getrandbits(8) & 0xF6, 1)]
        fields.append((tile(x, y) + 6, 0x10 | rng.randrange(16), 1))
    elif rng.random() < 0.3:
        fields.append((tile(x, y) + 7, rng.randint(0x5A, 0x5C), 1))  # a ripe field to harvest (job 0x96)
    # The door tile's area and the weapon piles (after the patches).
    fields.append((tile(door_x, door_y) + 0x14, area, 2))
    full = rng.random() < 0.3  # the weapon piles have no room
    for k, dx in ((1, 0), (2, 1), (3, -1)):
        fields += [(tile(door_x + dx, door_y) + 2, piles[k], 2),
                   (PILES + piles[k] * PILE_BYTES + 1, rng.randint(3, 8) if full else amounts[k] & 3, 1)]



# ---- family d (drinks, settler_jobs_drinks.cpp) ----
US_EXEMPT_D = (
    # 0x4f3c25: the flood fill of PioneerWork meets a tile the pioneer's
    # player already owns: dead, the same-team check before it
    # (0x4f3bd9..) skips every tile of the player's own team.
    0x004f3c25,
)

# Jobs of this family that need a prepared neighbourhood, drawn more often
# than the uniform choice of the generic part: geologist 0xd4, pioneer
# 0x12e, the field searches 0xe3 / 0xf4, planting 0xe6 / 0xf7, harvests
# 0xeb / 0xec / 0xfc / 0xfd.
_US_D_HARD = (0xd4, 0xd4, 0x12e, 0x12e, 0x12e, 0xe3, 0xe3, 0xf4, 0xf4, 0xf4, 0xf4, 0xe6, 0xf7, 0xf7, 0xf7,
              0xeb, 0xec, 0xfc, 0xfd)
_US_D_SIGN_RANGES = ((0x11, 0x17), (0x18, 0x1f), (0x21, 0x27), (0x28, 0x2f), (0x31, 0x37), (0x38, 0x3f),
                     (0x41, 0x47), (0x48, 0x4f), (0x51, 0x57), (0x58, 0x5f))


def _us_d_window(rng, fields, cx, cy, radius, area, terrains, ripe):
    """Tiles around (cx, cy) for the field searches and planting: mostly
    free tiles of the building's area, some ripe fields and obstacles."""
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            t = tile(cx + dx, cy + dy)
            r = rng.random()
            obj = rng.randint(ripe[0], ripe[1]) if r < 0.03 else 0 if r < 0.94 else rng.getrandbits(8)
            fields += [(t + 0x14, area if rng.random() < 0.93 else rng.randrange(US_AREAS), 2),
                       (t + 0x7, obj, 1),
                       (t + 0x2, 0 if rng.random() < 0.96 else rng.randint(1, US_OTHERS), 2),
                       (t + 0xa, 0 if rng.random() < 0.9 else rng.getrandbits(8), 1),
                       (t + 0x6, rng.choice(terrains) if rng.random() < 0.8 else rng.getrandbits(8), 1)]


def _us_d_pioneer(rng, fields, x, y, owner):
    """A patch around the pioneer: takeable foreign / free tiles (flags 1 or
    8, terrain with a high nibble) inside a ring of the player's own tiles,
    so that the edge walk and the flood fill stay inside."""
    for dx in range(-4, 5):
        for dy in range(-4, 5):
            d = hex_distance(dx, dy)
            if d > 4:
                continue
            t = tile(x + dx, y + dy)
            if d == 4 or (d == 3 and rng.random() < 0.5):
                fields += [(t + 0x8, owner, 1), (t + 0xa, 0, 1)]
                continue
            pick = rng.random()
            tile_owner = owner if pick < 0.25 else (owner ^ 1) if pick < 0.4 else rng.choice((2, 3, 0xFF))
            flags = rng.choice((1, 8, 9, 0x20 | 8, 0)) if rng.random() < 0.7 else rng.getrandbits(8) & 0xb2
            fields += [(t + 0x8, tile_owner, 1), (t + 0xa, flags, 1),
                       (t + 0x6, rng.choice((0x10, 0x20, 0x30, 0x15, 0x05, 0x06)), 1)]
    here = tile(x, y)
    fields += [(here + 0xa, 0 if rng.random() < 0.85 else rng.choice((1, 4, 8, 0x40)), 1),
               (here + 0x8, rng.choice((0xFF, 0xFF, owner, owner ^ 1, 2)), 1)]


def _us_d_big_fill(rng, fields, x, y, owner):
    """A takeable region of more than 10,000 tiles (radius 58) inside a ring
    of the player's tiles: the flood fill's ring buffer (tile_ring2, 10,000
    entries) wraps (0x4f3c8c, 0x4f3cbe)."""
    for dx in range(-59, 60):
        for dy in range(-59, 60):
            d = hex_distance(dx, dy)
            if d > 59:
                continue
            t = tile(x + dx, y + dy)
            if d == 59:
                fields += [(t + 0x8, owner, 1), (t + 0xa, 0, 1)]
            else:
                fields += [(t + 0x8, 0xFF, 1), (t + 0xa, 1, 1), (t + 0x6, 0x10, 1)]
    here = tile(x, y)
    fields += [(here + 0xa, 0, 1), (here + 0x8, 0xFF, 1)]


def _us_family_d(rng, fields, n, ctx):
    s, x, y = ctx["record"], ctx["x"], ctx["y"]
    job = rng.choice(_US_D_HARD) if rng.random() < 0.6 else ctx["job"]
    owner = rng.randrange(4)
    fields += [(s + 0xe, job, 2), (s, owner, 1), (s + 0xc, 0, 2), (s + 0x14, rng.getrandbits(7), 1)]
    # Teams: players 0 and 1 against 2 and 3.
    for p in range(4):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, p // 2, 4))
    # The work building.
    bn = rng.randint(1, US_OTHERS)
    b = BUILDINGS + bn * BUILDING_BYTES
    area = rng.randint(1, US_AREAS - 1)
    if rng.random() < 0.03:
        wx, wy = rng.choice((1, 2, 3, 381, 382)), rng.randint(3, 380)
    else:
        wx, wy = x + rng.randint(-3, 3), y + rng.randint(-3, 3)
    dx, dy = wx + rng.randint(-2, 2), wy + rng.randint(-2, 2)
    pile = rng.randint(1, US_PILES)
    fields += [(s + 0x22, bn, 2), (s + 0x17, rng.choice((0, 0, 1, 1, 2, rng.getrandbits(8))), 1),
               (s + 0x10, rng.choice((0, rng.randrange(40), rng.randrange(40), rng.randint(100, 149),
                                      rng.randint(350, 399))), 2),
               (b + 0x18, 1 | rng.getrandbits(8) if rng.random() < 0.9 else 0, 1),
               (b + 0x30, wx, 2), (b + 0x32, wy, 2), (b + 0xa, dx, 2), (b + 0xc, dy, 2),
               (b + 0xe, pile, 2), (PILES + pile * PILE_BYTES + 1, rng.choice((0, 3, 7, 8, 9)), 1),
               (tile(dx, dy) + 0x14, area, 2)]
    if job == 0xd4:
        if rng.random() < 0.1:
            x = rng.randint(0, 2)  # signs off the map (IsOnMap fails, 0x4f3584)
            fields.append((s + 0x4, x, 2))
        # The geologist's tile and the signs within distance 4.
        lo, hi = rng.choice(_US_D_SIGN_RANGES)
        res = 0 if rng.random() < 0.1 else rng.randint(lo, hi) if rng.random() < 0.85 else rng.getrandbits(8)
        sign = 0xe9 + 2 * ((lo - 0x11) // 16)
        density = rng.choice((0.0, 0.1, 0.3, 0.9))
        for ddx in range(-4, 5):
            for ddy in range(-4, 5):
                if 0 < hex_distance(ddx, ddy) <= 4:
                    fields.append((tile(x + ddx, y + ddy) + 0x7,
                                   rng.choice((sign, sign + 1)) if rng.random() < density else 0, 1))
        fields.append((tile(x, y) + 0x11, res, 1))
        if rng.random() < 0.1:
            fields.append((NEIGHBOUR_OFFSETS + 16 + 8, 5, 4))  # no ring within distance 4
    elif job == 0x12e:
        if n == 1 and rng.random() < 0.02:
            x, y = rng.randint(62, 320), rng.randint(62, 320)
            fields += [(s + 0x4, x, 2), (s + 0x6, y, 2)]
            _us_d_big_fill(rng, fields, x, y, owner)
        else:
            _us_d_pioneer(rng, fields, x, y, owner)
    elif job in (0xe3, 0xe6):
        _us_d_window(rng, fields, wx, wy, 5, area, (0x15,), (0x6c, 0x6e))
        if job == 0xe6:
            _us_d_window(rng, fields, x, y, 1, area, (0x15,), (0x6c, 0x6e))
    elif job in (0xf4, 0xf7):
        _us_d_window(rng, fields, wx, wy, 6, area, (0x10, 0x16, 0x18), (0x63, 0x65))
        if job == 0xf7:
            _us_d_window(rng, fields, x, y, 3, area, (0x10, 0x16, 0x18), (0x63, 0x65))
    elif job in (0xeb, 0xfc):
        ripe = (0x6c, 0x6e) if job == 0xeb else (0x63, 0x65)
        fields.append((tile(x, y) + 0x7, rng.randint(*ripe) if rng.random() < 0.6 else rng.getrandbits(8), 1))


# ---- family s (special, settler_jobs_special.cpp) ----
US_EXEMPT_S = ()

US_TRADES = 0x10f45e8
US_TRADE_BYTES = 0x5a
US_OFFSETS = 0x110a588
# The jobs of the family, the long handlers drawn more often.
US_SPECIAL_JOBS = (
    0x17, 0x8d, 0x145, 0x146, 0x130, 0x134, 0x136, 0x14b, 0x14c, 0x13a, 0x13b, 0x13c, 0x13e, 0x13d, 0x13f, 0x143,
    0x144, 0x142, 0x150, 0x14d, 0x14e, 0x14f, 0x151, 0x152, 0x193, 0x194, 0x161, 0x163, 0x164, 0x166, 0x167, 0x169,
    0x16b, 0x16c, 0x16e, 0x16f, 0x171, 0x153, 0x155, 0x156, 0x15f, 0x157, 0x158, 0x159, 0x15a, 0x15b, 0x15c, 0x15d,
    0x15e, 0x160, 0x17c, 0x17e, 0x17f, 0x180, 0x181, 0x182, 0x183, 0x184, 0x185, 0x190, 0x191, 0x192,
    0x134, 0x136, 0x136, 0x14b, 0x14b, 0x14c, 0x14c, 0x14d, 0x14d, 0x163, 0x166, 0x166, 0x143, 0x192, 0x192,
    0x15a, 0x15b, 0x152, 0x152,
    # The handlers with rare branches (scenarios in _us_special_scenario) once more.
    0x14b, 0x14b, 0x14b, 0x14d, 0x14d, 0x152, 0x152, 0x152, 0x155, 0x155, 0x155, 0x163, 0x163, 0x136, 0x136,
    0x143, 0x143, 0x191, 0x191, 0x152, 0x152, 0x152, 0x14b, 0x14b, 0x17e, 0x17e, 0x14c, 0x14c, 0x183, 0x183, 0x15a, 0x134, 0x134)
US_SPECIAL_TYPES = (0x13, 0x15, 0x17, 0x2d, 0x2f, 0x2e, 0x2b, 0x2c, 0x27, 0x08, 0x21)
US_DIR_X = (1, 1, 0, -1, -1, 0)   # NeighbourX/Y step per direction (map_directions.cpp)
US_DIR_Y = (0, 1, 1, 0, -1, -1)


def _us_near(rng, v, r):
    return v - r + rng.randrange(2 * r + 1)


def _us_family_s(rng, fields, n, ctx):
    """The settler's building with a trade route (often ending here, its
    points in the settler's area and walk region), ring piles, door piles,
    the pile under the settler (whose +0xa names the building), flowers /
    hives / free spots around the work point, patients for the healer, a
    ship on the shipyard's ship tile; players 0..3 in two teams. As the
    in-process test's CustomizeSpecial (settler_jobs_special.cpp)."""
    s = ctx["record"]
    sx, sy = ctx["x"], ctx["y"]
    job = rng.choice(US_SPECIAL_JOBS)
    art = rng.choice(US_SPECIAL_TYPES) if rng.random() < 0.8 else rng.randrange(0x30)
    owner = rng.randrange(4)
    building = rng.randint(1, US_OTHERS)
    b = BUILDINGS + building * BUILDING_BYTES
    fields += [(s, owner, 1), (s + 0x1, art, 1), (s + 0xe, job, 2), (s + 0x22, building, 2)]
    btype = rng.choice((0x21, 0x22, 0x21, 0x22, 0x20, 0x33, 0x34, 0x35, 0x23)) if rng.random() < 0.8 \
        else rng.randrange(0x39)
    kinds = [rng.choice((0, 1, 2, 3, 0x13, 250, 251, 0x10)) for _ in range(8)]
    if rng.random() < 1 / 3:  # an ordered gong, all steps done
        kinds[6], kinds[7] = 0x13, 0
    dock = rng.randrange(6) if rng.random() < 0.9 else rng.getrandbits(16)
    bx, by = _us_near(rng, sx, 4), _us_near(rng, sy, 4)
    wx, wy = _us_near(rng, sx, 3), _us_near(rng, sy, 3)
    dx, dy = _us_near(rng, sx, 3), _us_near(rng, sy, 3)
    trade = rng.randint(1, 20)
    fields += [(b, owner if rng.random() < 0.8 else rng.randrange(4), 1), (b + 0x2, bx, 2), (b + 0x4, by, 2),
               (b + 0x6, btype, 1), (b + 0xa, dx, 2), (b + 0xc, dy, 2), (b + 0x18, rng.getrandbits(8), 1),
               (b + 0x21, 0 if rng.random() < 0.8 else 1, 1), (b + 0x22, dock, 2),
               (b + 0x30, wx, 2), (b + 0x32, wy, 2), (b + 0x34, trade, 2)]
    fields += [(b + 0x19 + k, kinds[k], 1) for k in range(8)]
    # Ring piles.
    ring = [rng.randint(1, US_PILES) for _ in range(6)]
    fields.append((b + 0xe, ring[0], 2))
    for k, p in enumerate(ring):
        r = PILES + p * PILE_BYTES
        fields += [(r + 0xc, ring[(k + 1) % 6], 2), (r + 1, 0 if rng.random() < 1 / 3 else rng.randrange(9), 1),
                   (r + 2, 0 if rng.random() < 0.5 else rng.randrange(4), 1)]
    # The settler's own tile: area and walk region shared with the route.
    area, region = rng.randrange(US_AREAS), rng.randint(1, 3)
    here = tile(sx, sy)
    route = []
    at_end = rng.random() < 0.75
    for k in range(4):
        unset = rng.random() < 0.25 and not (k == 3 and at_end)
        route.append((0xFFFF, 0xFFFF) if unset else (_us_near(rng, sx, 6), _us_near(rng, sy, 6)))
    if at_end:
        route[3] = (sx, sy)
    same = rng.random() < 0.8
    t = US_TRADES + trade * US_TRADE_BYTES
    for k, (x, y) in enumerate(route):
        fields += [(t + 2 + 2 * k, x, 2), (t + 0xa + 2 * k, y, 2)]
    for x, y in route:
        if x != 0xFFFF:
            fields += [(tile(x, y) + 0x14, area if same else rng.randrange(US_AREAS), 2),
                       (tile(x, y) + 0x10, region if same else rng.randint(1, 3), 1)]
    # The pile under the settler names the building (pile 0 too).
    tile_pile = rng.randint(1, US_PILES) if rng.random() < 2 / 3 else 0
    tp = PILES + tile_pile * PILE_BYTES
    fields += [(tp + 0xa, building, 2), (PILES + 0xa, building, 2), (tp + 1, rng.randrange(5), 1),
               (tp + 2, rng.randrange(3), 1)]
    # Door piles.
    for side in (-1, 1):
        p = rng.randrange(US_PILES + 1)
        fields += [(tile(dx + side, dy) + 2, p, 2), (PILES + p * PILE_BYTES + 1, rng.randrange(9), 1)]
    fields += [(s + 0x10, rng.randrange(200) if rng.random() < 0.9 else rng.getrandbits(16), 2),
               (s + 0x15, 0 if rng.random() < 0.5 else rng.randrange(8), 1),
               (s + 0x1a, 0 if rng.random() < 1 / 3 else 1 if rng.random() < 0.5 else rng.randrange(12), 1)]
    # Flowers, hives and free spots around the work point.
    door_area = rng.randrange(US_AREAS)
    fields.append((tile(dx, dy) + 0x14, door_area, 2))
    if rng.random() < 0.5:
        fields += [(tile(sx, sy - 2) + 7, 0 if rng.random() < 0.5 else 250 + rng.randrange(4), 1),
                   (tile(sx, sy - 2) + 0xa, rng.getrandbits(8) & 0xf6, 1),
                   (tile(sx, sy - 2) + 0x14, door_area, 2), (here + 0xa, rng.getrandbits(8) & 0xf6, 1)]
    for _ in range(6):
        x, y = _us_near(rng, wx, 3), _us_near(rng, wy, 3)
        fields += [(tile(x, y) + 7, rng.choice((250, 251, 252, 253, 0x44, 0x4a, 0x52, 0, 247, 0xf8)), 1),
                   (tile(x, y) + 0xa, rng.getrandbits(8) & 0xf6, 1),
                   (tile(x, y + 1) + 7, 0, 1), (tile(x, y + 1) + 0xa, rng.getrandbits(8) & 0xf6, 1),
                   (tile(x, y + 1) + 0x14, door_area, 2), (tile(x, y + 2) + 0x14, door_area, 2),
                   (tile(x, y + 3) + 0xa, rng.getrandbits(8) & 0xf6, 1)]
    # Teams 0/1.
    for p in range(4):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 0xc, p & 1, 4))
    # Patients near the door and the work point.
    for k in range(4):
        m = rng.randint(4, US_OTHERS)
        r = SETTLERS + m * SETTLER_BYTES
        px, py = (dx, dy) if k == 0 and rng.random() < 0.5 else (_us_near(rng, wx, 5), _us_near(rng, wy, 5))
        fields += [(r, owner, 1), (r + 1, 5, 1), (r + 3, 100 if rng.random() < 0.25 else rng.randrange(100), 1),
                   (r + 0xe, 0x37 if rng.random() < 0.75 else 1, 2), (tile(px, py), m, 2)]
    # A ship two steps from the work point, away from the landing.
    if dock < 6 and rng.random() < 0.75:
        away = (dock + 3) % 6
        shx, shy = wx + 2 * US_DIR_X[away], wy + 2 * US_DIR_Y[away]
        m = rng.randint(4, US_OTHERS)
        r = SETTLERS + m * SETTLER_BYTES
        fields += [(r + 1, rng.choice((0x2b, 0x2c)), 1), (r + 0xe, 0x145 if rng.random() < 0.75 else 0x130, 2),
                   (tile(shx, shy), m, 2)]
    # Last: the settler's own tile.
    fields += [(here + 0x14, area, 2), (here + 0x10, region, 1), (here + 2, tile_pile, 2)]
    _us_special_scenario(rng, fields, n, job, owner, s, sx, sy, b, wx, wy, dx, dy, ring, tile_pile, door_area)


def _us_special_scenario(rng, fields, n, job, owner, s, sx, sy, b, wx, wy, dx, dy, ring, tile_pile, door_area):
    """Rare branches of single jobs."""
    if job == 0x143 and rng.random() < 0.6:  # shipyard: the last step launches a trade ship or a ferry (0x4f72eb)
        dock = rng.randrange(6)
        away = (dock + 3) % 6
        m = rng.randint(4, US_OTHERS)
        r = SETTLERS + m * SETTLER_BYTES
        fields += [(b + 0x22, dock, 2), (b + 0x20, 0, 1), (s + 0x1a, 1, 1),
                   (r + 1, rng.choice((0x2b, 0x2c)), 1), (r + 0xe, 0x145, 2),
                   (tile(wx + 2 * US_DIR_X[away], wy + 2 * US_DIR_Y[away]), m, 2)]
    elif job in (0x17e, 0x183) and rng.random() < 0.6:  # mead maker: wants work, room in the output (0x4f8be4, 0x4f8e8c)
        fields += [(b + 0x18, 1, 1), (PILES + ring[0] * PILE_BYTES + 1, rng.randrange(8), 1)]
    elif job == 0x14c and rng.random() < 0.6:  # ship unloads at its own finished harbour (0x4f6ca6)
        fields += [(b, owner, 1), (b + 0x6, 0x21, 1), (b + 0x21, 0, 1)]
    elif job == 0x15a and rng.random() < 0.5:  # alchemist: the yield doesn't fit the door piles (0x4f86d7)
        for side in (-1, 1):
            p = rng.randint(1, US_PILES)
            fields += [(tile(dx + side, dy) + 2, p, 2), (PILES + p * PILE_BYTES + 1, rng.randint(6, 8), 1)]
    elif job == 0x134 and tile_pile != 0 and rng.random() < 0.6:  # donkey takes the pile's last units (0x4f61e4)
        two = rng.random() < 0.5
        fields += [(s + 0x17, 1 if two else 0, 1), (PILES + tile_pile * PILE_BYTES + 1, 2 if two else 1, 1),
                   (PILES + tile_pile * PILE_BYTES + 2, 2 if two else 1, 1)]
    elif job == 0x191 and rng.random() < 0.6:  # gong: steps left, some inputs wanted (0x4f902f)
        fields += [(b + 0x20, rng.randint(1, 5), 1)] + [(b + 0x1a + k, rng.choice((0, 0, 1, 2)), 1) for k in range(3)]
    elif job == 0x14b and rng.random() < 0.25:  # harbour: every ring pile empty, no cargo (0x4f69ac)
        fields += [(PILES + p * PILE_BYTES + 1, 0, 1) for p in ring]
    elif job == 0x14b and rng.random() < 0.5:  # harbour: race 1 ships take four cargo goods (0x4f67b5)
        fields.append((PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES + 4, 1, 4))
        fields += [(PILES + p * PILE_BYTES + 1, rng.randint(1, 8), 1) for p in ring]
        last = ring[0]  # the ring's end (a full turn): sometimes empty and not leaving
        if rng.random() < 0.5:
            fields += [(PILES + last * PILE_BYTES + 2, 0, 1)]
    elif job == 0x151:  # race 3 bowmen with arrows left (0x4f7861)
        fields += [(PLAYER_RECORDS + owner * PLAYER_RECORD_BYTES + 4, 3, 4), (s + 0x15, rng.randrange(3, 9), 1)]
    elif job == 0x152 and rng.random() < 0.25:  # thief: carries something (0x4f7a6f): generic presets
        pass
    elif job == 0x152 and rng.random() < 0.7:  # thief: nothing in reach (0x4f79e1), or only at offset 1000
        fields.append((s + 0x15, 0, 1))
        for ddx in range(-8, 9):
            for ddy in range(-8, 9):
                if hex_distance(ddx, ddy) <= 8:
                    fields.append((tile(sx + ddx, sy + ddy) + 2, 0, 2))
        if rng.random() < 0.85:
            o = US_OFFSETS + 1000 * 16
            p = rng.randint(1, US_PILES)
            # Off the cleared rings (distance 10).
            fields += [(o, 10, 4), (o + 4, 0, 4), (tile(sx + 10, sy) + 2, p, 2), (tile(sx + 10, sy) + 8, 0xFF, 1),
                       (tile(sx + 10, sy) + 0xa, rng.getrandbits(8) & 0xdf, 1),
                       (PILES + p * PILE_BYTES + 1, rng.randint(1, 8), 1),
                       (PILES + p * PILE_BYTES + 6, sx + (10 if rng.random() < 0.5 else 0), 2),
                       (PILES + p * PILE_BYTES + 8, sy, 2)]
    elif job == 0x152:  # thief: a pile on its own tile (0x4f7a15, 0x4f7a25)
        p = rng.randint(1, US_PILES)
        fields += [(s + 0x15, 0, 1), (tile(sx, sy) + 2, p, 2), (tile(sx, sy) + 8, 0xFF, 1),
                   (tile(sx, sy) + 0xa, rng.getrandbits(8) & 0xdf, 1), (PILES + p * PILE_BYTES + 1, rng.randint(1, 8), 1),
                   (PILES + p * PILE_BYTES + 6, sx, 2), (PILES + p * PILE_BYTES + 8, sy if rng.random() < 0.7 else sy + 1, 2)]
    elif job == 0x193 and tile_pile != 0:  # steal: its claim is released (0x4f7b57)
        k = rng.randrange(128)
        fields += [(0x1195dd4 + 4 * k, tile_pile, 2), (0x1195dd4 + 4 * k + 2, n, 2)]
    elif job == 0x14d and rng.random() < 0.25:  # healer: the search wraps at 2000 (0x4f76eb)
        fields += [(s + 0x10, rng.randint(1990, 1999), 2), (s + 0x1a, 0, 1), (b + 0x18, 1, 1), (tile(dx, dy), 0, 2)]
    elif job == 0x14d and rng.random() < 0.6:  # healer: a patient at the first offset (0x4f76d8)
        m = rng.randint(4, US_OTHERS)
        r = SETTLERS + m * SETTLER_BYTES
        fields += [(s + 0x10, 0, 2), (s + 0x1a, 0, 1), (b + 0x18, 1, 1), (tile(dx, dy), 0, 2),
                   (r, owner, 1), (r + 1, 5, 1), (r + 3, rng.randrange(100), 1), (r + 0xe, 0x37, 2), (tile(wx, wy), m, 2)]
    elif job == 0x163 and rng.random() < 0.2:  # beekeeper: the search wraps at 1500 (0x4f7f2f)
        fields += [(s + 0x10, rng.randint(1455, 1499), 2), (b + 0x18, 1, 1)]
    elif job == 0x163 and rng.random() < 0.7:  # beekeeper: a flower field or a hive spot at offset 0 (0x4f7fde, 0x4f800c)
        hive = rng.random() < 0.6
        fields += [(s + 0x10, 0, 2), (b + 0x18, 1, 1), (tile(dx, dy) + 0x14, door_area, 2),
                   (PILES + ring[0] * PILE_BYTES + 1, rng.randrange(8), 1)]
        if hive:
            for ddx in range(-4, 5):
                for ddy in range(-4, 5):
                    if hex_distance(ddx, ddy) <= 4:
                        fields.append((tile(wx + ddx, wy + 1 + ddy) + 7, 0, 1))
            if rng.random() < 0.3:  # a hive or flowers too near: the spot is not taken (0x4f7eba)
                ddx, ddy = rng.choice(((1, 0), (0, 1), (-1, 1), (2, 0), (0, 2), (1, 1)))
                fields.append((tile(wx + ddx, wy + 1 + ddy) + 7, rng.randint(247, 253), 1))
        fields += [(tile(wx, wy) + 7, rng.randint(0x44, 0x52) if hive else rng.randint(250, 253), 1),
                   (tile(wx, wy + 1) + 0x14, door_area, 2), (tile(wx, wy + 1) + 0xa, rng.getrandbits(8) & 0xf6, 1),
                   (tile(wx, wy + 2) + 0x14, door_area, 2), (tile(wx, wy + 3) + 0xa, rng.getrandbits(8) & 0xf6, 1)]
    elif job == 0x155 and rng.random() < 0.5:  # alchemist: both door piles full enough to wait (0x4f8472)
        for side in (-1, 1):
            p = rng.randint(1, US_PILES)
            fields += [(tile(dx + side, dy) + 2, p, 2), (PILES + p * PILE_BYTES + 1, rng.randint(6, 8), 1)]



US_FAMILY_SETUP = {"u": _us_family_u, "b": _us_family_b, "m": _us_family_m, "f": _us_family_f,
                   "d": _us_family_d, "s": _us_family_s, "-": lambda rng, fields, n, ctx: None}
US_EXEMPT = US_EXEMPT_U + US_EXEMPT_B + US_EXEMPT_M + US_EXEMPT_F + US_EXEMPT_D + US_EXEMPT_S


def update_settlers_world(rng, pool, args):
    """One case: 1..3 settlers with jobs of one family (S3RE_US_FAMILY=<letters>
    restricts the families, for working on one family's presets)."""
    import os
    families = os.environ.get("S3RE_US_FAMILY") or "uuubbbmmmfffdddsss-"
    family = rng.choice(families)
    count = rng.randint(1, 3)
    fields = [(MAP_SIZE, 384, 4), (SETTLER_COUNT, count, 4)] + walk_rings(8)
    for p in range(4):
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, rng.randrange(4), 4))
    for b in range(1, US_OTHERS + 1):
        _us_building(rng, fields, b)
    for p in range(1, US_PILES + 1):
        _us_pile(rng, fields, p)
    for k in range(count + 1, US_OTHERS + 1):
        s = SETTLERS + k * SETTLER_BYTES
        fields += [(s + 0x4, rng.randint(40, 340), 2), (s + 0x6, rng.randint(40, 340), 2),
                   (s + 0x22, rng.randrange(US_OTHERS + 1), 2)]
    for n in range(1, count + 1):
        ctx = _us_settler(rng, fields, n, family)
        US_FAMILY_SETUP[family](rng, fields, n, ctx)
    return fields


def update_settlers_globals(rng, pool, args):
    return [(0x7dfd48, rng.getrandbits(32), 4)]  # g_gameTick


SPECS += [
    # The code ends at 0x4f926d; the jump tables follow it. Each case
    # exercises one job family (six families and the loop), so the default
    # run is 12 x 2000 = 24,000 cases: two 12,000-case runs with different
    # draws each left 5..7 rare blocks uncovered, disjoint between them.
    Spec("CGameData_UpdateSettlers", 0x004e9670, "thiscall", [], presets=update_settlers_world,
         globals=update_settlers_globals, callees=US_PROBES, case_scale=12, cover_until=0x004f926d,
         exempt_blocks=US_EXEMPT),
]


# --- session 46, package S1: the land spells (spells_land.cpp) ----------------
# IsTransformableTerrain, NextTransformTerrain and CanTerrainsBorder are pure
# tables (NextTransformTerrain reads the grid's kind and ends kinds 0 and 1
# in the assertion ThrowAssertmentFailed, which runs as S3.EXE code here and
# ends the case as a throw). SpreadTerrainTransform gets a grid in the
# scratch area (0x2018 bytes from SCRATCH) and the world's tile ring; its
# callees run as S3.EXE code. TransformTerrainAround: a 32 x 32 grid of
# terrain patches around (x, y); UnblockTile and WearPath (verified on
# their own) are probed: on a lazily random world their region flood fills
# are meaningless. SpellTransformLand probes TransformTerrainAround (its own
# spec). The stone and tree spells probe BlockTile (as above) and the CRT's
# rand() 0x61f802 (its per-thread data comes through TlsGetValue, which the
# emulator doesn't provide; the probe's random values also reach the
# signed-remainder fix-up of rand() % 8).
LAND_TERRAINS = [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1c, 0x20, 0x21, 0x22, 0x23, 0x30,
                 0x40, 0x41, 0x50, 0x51, 0x60, 0x61, 0x62, 0x63, 0x70, 0x71, 0x80, 0x81, 0x90, 0x91, 0x00, 0x05,
                 0x1f]
TRANSFORM_KINDS = [0, 1, 2, 3, 32]
# IsTransformableTerrain's terrains per kind.
TRANSFORM_SOURCES = {0: (0x50, 0x51, 0x15), 1: (0x80, 0x81, 0x23), 2: (0x10, 0x12, 0x13, 0x41, 0x14, 0x18, 0x1c, 0x16),
                     3: (0x40, 0x41, 0x14), 32: (0x10, 0x12, 0x13, 0x18, 0x17, 0x91)}
TERRAIN_GRID = SCRATCH  # TerrainGrid: kind, x0, y0, cx, cy, side, can_change[64][64], terrain[64][64]
TILE_RING_INDEX = 0xe07888


def land_terrain(rng) -> int:
    return rng.randrange(256) if rng.random() < 0.1 else rng.choice(LAND_TERRAINS)


def land_kind(rng, pool) -> int:
    if rng.random() < 0.1:
        return anything(rng, pool)
    return rng.choice(TRANSFORM_KINDS + [4, 30]) if rng.random() < 0.6 else rng.randrange(40)


def land_value(rng, pool) -> int:
    r = rng.random()
    if r < 0.6:
        return rng.choice(LAND_TERRAINS)
    if r < 0.75:
        return 0xFF
    return rng.randrange(256) if r < 0.9 else anything(rng, pool)


def terrain_grid(rng, pool, args):
    """A grid of a kind with patches of terrain; marks 0 on the border. args:
    (grid, gx, gy) with an interior start."""
    gx, gy = args[1], args[2]
    side = 32 if rng.random() < 0.7 else rng.randint(max(gx, gy) + 2, 64)
    kind = rng.choice(TRANSFORM_KINDS) if rng.random() < 0.9 else rng.randrange(40)
    fields = [(TERRAIN_GRID, kind, 4), (TERRAIN_GRID + 0x14, side, 4)]
    base = land_terrain(rng)
    mix = rng.randrange(4)
    follow = rng.random() < 0.75
    sources = TRANSFORM_SOURCES.get(kind, ())
    for x in range(side):
        terrain, marks = [], []
        for y in range(64):
            t = land_terrain(rng) if rng.randrange(4) < mix else base
            terrain.append(t)
            border = x in (0, side - 1) or y in (0, side - 1) or y >= side
            mark = (1 if t in sources else 0) if follow else (rng.randrange(4) if rng.random() < 0.25 else 1)
            marks.append(0 if border else mark)
        for y in range(0, 64, 4):
            fields.append((TERRAIN_GRID + 0x18 + x * 64 + y, int.from_bytes(bytes(marks[y:y + 4]), "little"), 4))
            fields.append((TERRAIN_GRID + 0x1018 + x * 64 + y, int.from_bytes(bytes(terrain[y:y + 4]), "little"), 4))
    return fields


def land_tile_fields(rng, fields, x, y, terrain, busy):
    """Tile (x, y): settler/building words, terrain and object, flags."""
    t = tile(x, y)
    obj = rng.randrange(256) if rng.randrange(1000) < busy else 0
    words = (rng.getrandbits(16) if rng.randrange(1000) < busy // 4 else 0) | \
        ((rng.getrandbits(16) if rng.randrange(1000) < busy // 4 else 0) << 16)
    fields.append((t, words, 4))
    fields.append((t + 4, rng.getrandbits(16) | terrain << 16 | obj << 24, 4))
    flags = rng.randrange(256) if rng.randrange(1000) < busy else 0
    fields.append((t + 8, rng.getrandbits(16) | flags << 16 | rng.getrandbits(8) << 24, 4))


def signed(value: int) -> int:
    return value - (1 << 32) if value >= 1 << 31 else value


def land_world(ring: int, spread: int, transform: bool):
    """A map with terrain patches and mostly empty land (or busy land) around
    (args[1], args[2]), `ring` ring offsets of at most `spread` tiles, the
    players' mana (spells: args[0] is the player) and the effect table."""
    def preset(rng, pool, args):
        fields = world_effect_table(rng, pool, args)
        size = rng.randint(40, 130) if rng.random() < 0.85 else rng.randint(20, 37)
        fields.append((SIGHT_MAP, size, 4))
        if not transform:
            record = PLAYER_RECORDS + (args[0] & 3) * PLAYER_RECORD_BYTES
            fields += [(record + 0x4, rng.randrange(4), 4), (record + 0x8c, rng.randrange(100), 4)]
            fields += [(record + 0x94 + slot * 4, rng.randrange(60), 4) for slot in range(9)]
        for n in range(ring):
            d = min(1 + n // 12, spread)
            fields += [(NEIGHBOUR_OFFSETS + n * 16, rng.randint(-d, d) & 0xFFFFFFFF, 4),
                       (NEIGHBOUR_OFFSETS + n * 16 + 4, rng.randint(-d, d) & 0xFFFFFFFF, 4)]
        x, y = signed(args[1]), signed(args[2])
        if transform:
            side, half = 32, 16
            x0 = min(max(x - half, 0), size - side)
            y0 = min(max(y - half, 0), size - side)
            xs, ys = range(x0 - 1, x0 + side + 1), range(y0 - 1, y0 + side + 1)
        else:
            reach = spread + 3
            xs = range(max(x - reach, -2), min(x + reach, size + 2) + 1)
            ys = range(max(y - reach, -2), min(y + reach, size + 2) + 1)
        base = land_terrain(rng)
        if transform and args[0] in TRANSFORM_SOURCES and rng.random() < 0.6:
            base = rng.choice(TRANSFORM_SOURCES[args[0]])  # land the kind can change
        mix = rng.randrange(5)
        busy = rng.randint(1, 20) if rng.random() < 0.5 else rng.randint(100, 500)
        for tx in xs:
            for ty in ys:
                land_tile_fields(rng, fields, tx, ty, land_terrain(rng) if rng.randrange(6) < mix else base, busy)
        return fields
    return preset


def land_coordinate(rng, pool) -> int:
    return rng.randint(-10, 140) & 0xFFFFFFFF if rng.random() < 0.15 else rng.randint(5, 60)


LAND_PROBES = {0x0051b770: (2, "thiscall", "probe"), 0x0051b7f0: (2, "thiscall", "probe")}
LAND_SPELL_PROBES = {0x0051b6f0: (2, "thiscall", "probe"), 0x0061f802: (0, "cdecl", "probe")}
LAND_TICK = lambda rng, pool, args: [(0x007dfd48, rng.getrandbits(32), 4)]  # noqa: E731 g_gameTick

SPECS += [
    Spec("CGameData_IsTransformableTerrain", 0x00548a10, "thiscall", [land_kind, land_value], eax_mask=0xFF),
    # The pure tables need more cases for every (kind or terrain, terrain)
    # pair that ends in its own block (cheap: about 2 s).
    Spec("CGameData_NextTransformTerrain", 0x00548bb0, "thiscall", [scratch(0), land_value, anything],
         eax_mask=0xFF, presets=lambda rng, pool, args: [(TERRAIN_GRID, land_kind(rng, pool), 4)], case_scale=5),
    Spec("CGameData_CanTerrainsBorder", 0x00548d50, "thiscall", [anything, land_value, land_value], eax_mask=0xFF,
         case_scale=10),
    # Exempt: 0x548989 and 0x5489bb (the tile ring's write and read index
    # wrapping at 10000): dead, every fill starts at 0 / 1 and queues each
    # cell of the grid (at most 64 x 64 = 4096) at most once.
    Spec("CGameData_SpreadTerrainTransform", 0x00548760, "thiscall",
         [scratch(0), between(1, 30), between(1, 30)], eax_mask=0, presets=terrain_grid, max_cases=600,
         exempt_blocks=(0x548989, 0x5489bb)),
    Spec("CGameData_TransformTerrainAround", 0x00548380, "thiscall",
         [one_of(*TRANSFORM_KINDS), land_coordinate, land_coordinate], eax_mask=0xFF,
         presets=land_world(210, 16, True), callees=LAND_PROBES, max_cases=300),
    Spec("CGameData_SpellTransformLand", 0x00548340, "thiscall",
         [below(4), land_coordinate, land_coordinate, below(4)], eax_mask=0,
         presets=land_world(0, 1, False), callees={0x00548380: (3, "thiscall", "probe")}, max_cases=300),
    Spec("CGameData_SpellCreateStones", 0x00548fb0, "thiscall", [below(4), land_coordinate, land_coordinate],
         eax_mask=0, presets=land_world(500, 20, False), callees=LAND_SPELL_PROBES, globals=LAND_TICK,
         ignore_bytes=tuple(WORLD_EFFECTS + b for b in EFFECT_TABLE_PADDING), max_cases=300),
    Spec("CGameData_SpellTreeCurse", 0x005492a0, "thiscall", [below(4), land_coordinate, land_coordinate],
         eax_mask=0, presets=land_world(1000, 20, False), callees=LAND_SPELL_PROBES, globals=LAND_TICK,
         ignore_bytes=tuple(WORLD_EFFECTS + b for b in EFFECT_TABLE_PADDING), max_cases=300),
]


# The helpers of CLogicThread::ExecuteCommands (src/s3re/game/command_helpers.cpp,
# re/execute_commands.md). Their callees are verified replacements with own
# specs; the ones with world effects are probed (calls compared).
CARRIER_PROBES = {0x0050cf50: (1, "thiscall", "probe"), 0x004def80: (1, "thiscall", "probe")}
CARRIER_JOBS = (10, 11, 12, 13, 80, 169, 170, 9, 14, 79, 81, 168, 171, 0x10a)


def carriers_of_building(rng, pool, args):
    """Settlers 1..count (count 0..30) with owners, the argument's building or another, carrier jobs or
    others; the building a site or not."""
    count = rng.randrange(31)
    fields = [(SETTLER_COUNT, count, 4), (BUILDINGS + args[0] * BUILDING_BYTES + 0x21,
                                          0 if rng.random() < 0.5 else rng.randrange(1, 256), 1)]
    for n in range(1, count + 2):
        s = SETTLERS + n * SETTLER_BYTES
        fields += [(s, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
                   (s + 0x22, args[0] if rng.random() < 0.6 else rng.randrange(0x10000), 2),
                   (s + 0xE, rng.choice(CARRIER_JOBS), 2)]
    return fields


def sites_in_areas(rng, pool, args):
    """Buildings 1..count (count 0..30) of a few owners with doors on a 4 x 4 patch of areas 0..2, sites or
    not, request flags 0/1 or random."""
    count = rng.randrange(31)
    fields = [(BUILDING_COUNT, count, 4)]
    for n in range(1, 32):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, rng.randrange(3), 1), (b + 0xA, rng.randrange(4), 2), (b + 0xC, rng.randrange(4), 2),
                   (b + 0x21, 0 if rng.random() < 0.3 else rng.randrange(1, 10), 1),
                   (b + 0x18, rng.randrange(2) if rng.random() < 0.5 else rng.randrange(256), 1)]
    for x in range(4):
        for y in range(4):
            fields.append((tile(x, y) + 0x14, rng.randrange(3), 2))
    return fields


def pile_for_good_change(rng, pool, args):
    p = PILES + args[0] * PILE_BYTES
    return [(p + 1, 0 if rng.random() < 0.6 else rng.randrange(1, 9), 1),
            (p + 3, 0 if rng.random() < 0.6 else rng.randrange(1, 9), 1),
            (p, 0xFF if rng.random() < 0.4 else rng.randrange(40), 1)]


def pile_good_arg(rng, pool):
    good = 0xFF if rng.random() < 0.4 else rng.randrange(40)
    return good | (rng.getrandbits(24) << 8 if rng.random() < 0.25 else 0)


def soldier_levels(rng, pool, args):
    p = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    return [(p + 0xB4 + 4 * k, (rng.randrange(-1, 4) if rng.random() < 0.85 else rng.getrandbits(32)) & 0xFFFFFFFF, 4)
            for k in (1, 2, 3)]


def owned_tile(rng, pool, args):
    return [(tile(args[0], args[1]) + 8, 0xFF if rng.random() < 0.5 else rng.randrange(20), 1)]


DOCK_MAP = 128
DOCK_REACH = 40   # tiles preset around the chosen tile (walks end at a shore long before)


def dock_coordinate(rng, pool):
    return rng.randrange(45, 84)


def dock_world(rng, pool, args):
    """A 128-tile map; around the chosen tile water (5), shore (0x30) and land in patches, areas 0..2 (half
    0), a few line markers (0x72); the offset table rings 0..9 (91 entries for the candidates, the marker
    check stops at distance 10); the building's door on the patch and its owner."""
    x0, y0 = args[1], args[2]
    fields = [(MAP_SIZE, DOCK_MAP, 4)] + walk_rings(10)
    for x in range(x0 - DOCK_REACH, x0 + DOCK_REACH + 1):
        for y in range(y0 - DOCK_REACH, y0 + DOCK_REACH + 1):
            r = rng.randrange(10)
            terrain = 5 if r < 4 else 0x30 if r < 7 else rng.randrange(64)
            t = tile(x, y)
            fields += [(t + 6, terrain, 1), (t + 0x14, 0 if rng.random() < 0.5 else rng.randrange(1, 3), 2),
                       (t + 7, 0x72 if rng.random() < 0.02 else rng.randrange(100), 1)]
    b = BUILDINGS + args[0] * BUILDING_BYTES
    fields += [(b, rng.randrange(20), 1), (b + 0xA, rng.randrange(x0 - 10, x0 + 11), 2),
               (b + 0xC, rng.randrange(y0 - 10, y0 + 11), 2)]
    return fields


CONDITION_KINDS = tuple(range(53))


def condition_kind(rng, pool):
    return rng.choice(CONDITION_KINDS) if rng.random() < 0.9 else rng.getrandbits(32)


SPECS += [
    Spec("CGameData_RecallSiteCarriers", 0x004dee20, "thiscall", [between(1, 40)],
         presets=carriers_of_building, callees=CARRIER_PROBES, max_cases=800),
    Spec("CGameData_RecallBuildingCarriers", 0x004deec0, "thiscall", [between(1, 40)],
         presets=carriers_of_building, callees=CARRIER_PROBES, max_cases=800),
    Spec("CGameData_RecallCarriersOfOtherSites", 0x004df8e0, "thiscall", [between(1, 31)], eax_mask=0,
         presets=sites_in_areas, callees={0x004dee20: (1, "thiscall", "probe")}, max_cases=800),
    Spec("CGameData_ChangePileGood", 0x004dfb40, "thiscall", [between(1, 1000), pile_good_arg], eax_mask=0,
         presets=pile_for_good_change,
         callees={0x00506080: (1, "thiscall", "probe"), 0x005061b0: (2, "thiscall", "probe")}),
    Spec("CGameData_SetPromotionCost", 0x00508450, "thiscall", [below(20)], presets=soldier_levels),
    Spec("CGameData_OpenDockLineTile", 0x004dbab0, "thiscall", [below(384), below(384), anything], eax_mask=0,
         presets=owned_tile,
         callees={0x0051b640: (2, "thiscall", "probe"), 0x0051b5d0: (3, "thiscall", "probe"),
                  0x0051b770: (2, "thiscall", "probe")}),
    Spec("CGameData_PlaceDockLine", 0x004db710, "thiscall", [between(1, 40), dock_coordinate, dock_coordinate],
         eax_mask=0, presets=dock_world, callees={0x004dbab0: (3, "thiscall", "probe")}, max_cases=300),
    Spec("CGameData_ResetAreaConditionCounters", 0x0051bc20, "thiscall", [below(64), condition_kind],
         case_scale=2),
]


# -- CLogicThread::ExecuteCommands 0x535990 (execute_commands*.cpp) ---------
#
# One spec for the whole function. The fake CLogicThread (LOGIC_THREAD) gets
# a step (+0x3b0) and its two MessageQueues: network -> logic (+0x3c8,
# vtable = emu.FAKE_VTABLE: slot 3 is the wait, probed) holding the case's
# messages, logic -> network (+0x3c4) collecting the pushes. The game's own
# MessageQueue::Pop (0x430980) and Push (0x4309c0) run on them; their
# critical section wrappers (0x60d670 / 0x60d680) are probed, and Push's
# SetEvent call (0x430b5e, `call [SetEvent]`) is replaced by `pop ecx; mov
# eax, 1` for the case (the event handle is pushed, SetEvent would succeed).
# Every case ends with a StepGrant, so the loop ends.
#
# Messages come from EC_COMMANDS: command id -> generator(rng, fields) that
# returns the payload bytes and appends the world fields the command needs
# to `fields`. Each part of the function adds its commands there.

EC_TO_NET = LOGIC_THREAD + 0x1000
EC_FROM_NET = LOGIC_THREAD + 0x1040
EC_EVENT = LOGIC_THREAD + 0x1080
EC_FROM_RING = LOGIC_THREAD + 0x2000
EC_TO_RING = LOGIC_THREAD + 0x3000
EC_RING_DWORDS = 1024
EC_STEP = 0x3b0
EC_SET_EVENT_CALL = 0x00430b5e
EC_SET_EVENT_STUB = int.from_bytes(b"\x59\xb8\x01\x00\x00\x00", "little")  # pop ecx; mov eax, 1
EC_BUILDINGS = 40
EC_SETTLERS = 120
EC_MAP = 384
REFRESH_UI = 0x2d27c
UI_SELECTION_MODE = 0x2d284
UI_PAGE_290 = 0x2d290
UI_PAGE_2A0 = 0x2d2a0
CAMPAIGN_MISSION = 0x7dfdb0
SELECTED_BUILDING = 0x7dfd6c  # g_selectedBuilding
REVEAL_MAP = 0x7dfd84
PLAYER_LISTS = 0x118a47c
PLAYER_LIST_LENGTHS = 0x11940bc

EC_COMMANDS: dict = {}
EC_WEIGHTS: dict = {0x12: 6}  # SetTradeRoute: eleven kinds


def ec_hi(v: int) -> int:
    return (v >> 8) & 0xFF


def ec_lo(v: int) -> int:
    return v & 0xFF


def ec_player(rng) -> int:
    return rng.randrange(20) if rng.random() < 0.1 else rng.randrange(4)


def ec_building(rng, fields: list, n: int | None = None) -> int:
    """A building 0..40 with fields the commands look at (on the map, owner 0..3)."""
    n = rng.randrange(EC_BUILDINGS + 1) if n is None else n
    b = BUILDINGS + n * BUILDING_BYTES
    types = (1, 2, 3, 5, 14, 15, 16, 18, 24, 25, 30, 31, 32, 33, 36, 47, 48, 50, 51, 4, 7)
    fields += [(b + 0x00, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
               (b + 0x02, rng.randrange(2, EC_MAP - 2), 2), (b + 0x04, rng.randrange(2, EC_MAP - 2), 2),
               (b + 0x06, rng.choice(types), 1),
               (b + 0x08, 0 if rng.random() < 0.4 else rng.randrange(EC_SETTLERS), 2),
               (b + 0x0a, rng.randrange(EC_MAP), 2), (b + 0x0c, rng.randrange(EC_MAP), 2),
               (b + 0x1f, 0 if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x21, 0 if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x36, rng.randrange(5), 1)]
    return n


def ec_settler(rng, fields: list, n: int | None = None) -> int:
    n = rng.randrange(1, EC_SETTLERS + 1) if n is None else n
    s = SETTLERS + n * SETTLER_BYTES
    fields += [(s + 0x00, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
               (s + 0x01, rng.choice((0, 1, 5, 8, 15, 32, 13, 36, 36, 44, 44, 43, 22, 40)), 1),
               (s + 0x04, rng.randrange(10, EC_MAP - 10), 2), (s + 0x06, rng.randrange(10, EC_MAP - 10), 2),
               (s + 0x0e, rng.choice((0x37, 0x35, 0xb0, 0, 1, 0x36)), 2),
               (s + 0x10, rng.randrange(EC_SETTLERS), 2), (s + 0x15, rng.randrange(8), 1)]
    return n


def ec_tile(x: int, y: int) -> int:
    return TILES + (x * 0x300 + y) * 0x18


def ec_area(rng, fields: list) -> int:
    n = rng.randrange(16)
    fields.append((AREAS + n * AREA_BYTES, 0xFF if rng.random() < 0.2 else rng.randrange(4), 1))
    return n


def ec_place_building(rng, fields):
    x, y, player = rng.randrange(EC_MAP), rng.randrange(EC_MAP), ec_player(rng)
    for n in range(1, EC_BUILDINGS + 1):
        if rng.random() < 0.3:
            b = BUILDINGS + n * BUILDING_BYTES
            fields += [(b, player if rng.random() < 0.7 else rng.randrange(4), 1),
                       (b + 0x06, rng.choice((15, 16, 18, 2)), 1),
                       (b + 0x08, 0 if rng.random() < 0.3 else rng.randrange(1, 100), 2),
                       (b + 0x21, 0 if rng.random() < 0.7 else 1, 1)]
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), 0, rng.randrange(60), player]


def ec_scroll(rng, fields):
    return [rng.randrange(256) for _ in range(8)]


def ec_settler_group(rng, fields):
    player = rng.randrange(4)
    count = 0 if rng.random() < 0.2 else rng.randrange(30)
    payload = [player, rng.randrange(30 if rng.random() < 0.25 else 10), ec_hi(count), ec_lo(count)]
    for _ in range(count):
        s = rng.randrange(EC_SETTLERS)
        payload += [ec_hi(s), ec_lo(s)]
    return payload


def ec_building_working(rng, fields):
    n = ec_building(rng, fields)
    return [ec_hi(n), ec_lo(n), 0 if rng.random() < 0.5 else rng.randrange(3), ec_player(rng)]


def ec_work_area(rng, fields):
    n = ec_building(rng, fields, rng.randrange(1, 4) if rng.random() < 0.5 else None)
    b = BUILDINGS + n * BUILDING_BYTES
    x, y = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
    if rng.random() < 0.7:  # near the building, in the same area as its door
        bx, by = rng.randrange(40, EC_MAP - 40), rng.randrange(40, EC_MAP - 40)
        x, y = bx + rng.randrange(-30, 30), by + rng.randrange(-30, 30)
        dx, dy = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
        area = rng.randrange(3)
        fields += [(b + 0x02, bx, 2), (b + 0x04, by, 2), (b + 0x0a, dx, 2), (b + 0x0c, dy, 2),
                   (b + 0x06, rng.choice((2, 30, 14, 25, 36, 24, 5, 3, 51, 4)), 1),
                   (ec_tile(dx, dy) + 0x14, area, 2),
                   (ec_tile(x, y) + 0x14, area if rng.random() < 0.8 else area + 1, 2)]
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(n), ec_lo(n), ec_player(rng)]


def ec_area_setting(kinds: int):
    def generate(rng, fields):
        area = ec_area(rng, fields)
        return [rng.randrange(256) if rng.random() < 0.1 else rng.randrange(kinds),
                rng.randrange(256) if rng.random() < 0.2 else rng.randrange(2), ec_player(rng),
                ec_hi(area), ec_lo(area)]
    return generate


def ec_goods_priority(rng, fields):
    area = ec_area(rng, fields)
    good = rng.randrange(36)
    fields.append((AREAS + area * AREA_BYTES + 0x18a + good, rng.choice((0, 20, 5, 21, 255)), 1))
    return [good, rng.randrange(2), ec_player(rng), ec_hi(area), ec_lo(area)]


def ec_dock(rng, fields):
    n = ec_building(rng, fields)
    b = BUILDINGS + n * BUILDING_BYTES
    if rng.random() < 0.6:
        fields += [(b + 0x06, rng.choice((32, 33)), 1), (b + 0x21, 0, 1), (b + 0x1f, 0, 1)]
    x, y = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(n), ec_lo(n), ec_player(rng)]


def ec_cast_spell(rng, fields):
    player = ec_player(rng)
    s = ec_settler(rng, fields)
    sx, sy = rng.randrange(20, EC_MAP - 20), rng.randrange(20, EC_MAP - 20)
    fields += [(SETTLERS + s * SETTLER_BYTES, player if rng.random() < 0.8 else 5, 1),
               (SETTLERS + s * SETTLER_BYTES + 1, 36 if rng.random() < 0.8 else 44, 1),
               (SETTLERS + s * SETTLER_BYTES + 4, sx, 2), (SETTLERS + s * SETTLER_BYTES + 6, sy, 2)]
    p = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    slot = rng.randrange(8)
    fields += [(p + 4, rng.randrange(4), 4), (p + 0x8c, rng.randrange(100), 4),
               (p + 0x94 + slot * 4, rng.randrange(100), 4)]
    x, y = sx + rng.randrange(-25, 25), sy + rng.randrange(-25, 25)
    return [player, slot, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(s), ec_lo(s)]


def ec_ship(load: bool):
    def generate(rng, fields):
        player = ec_player(rng)
        s = ec_settler(rng, fields)
        base = SETTLERS + s * SETTLER_BYTES
        x, y = rng.randrange(20, EC_MAP - 20), rng.randrange(20, EC_MAP - 20)
        fields += [(base, player if rng.random() < 0.8 else 7, 1), (base + 4, x, 2), (base + 6, y, 2),
                   (base + 0x01, 44 if rng.random() < 0.8 else 43, 1), (base + 0x15, rng.randrange(7), 1)]
        if load:
            # Workshops around the ship, the offset table with growing distances.
            for k in range(60):
                dx, dy = rng.randrange(-5, 6), rng.randrange(-5, 6)
                fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                           (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4),
                           (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 4 if rng.random() < 0.97 else -1 & 0xFFFFFFFF, 4)]
                t = ec_tile(x + dx, y + dy)
                n = rng.randrange(1, EC_BUILDINGS + 1)
                fields += [(t + 0x0a, 0x20 if rng.random() < 0.7 else 0, 1),
                           (t + 0x02, n if rng.random() < 0.9 else 0, 2)]
                if rng.random() < 0.5:
                    b = BUILDINGS + n * BUILDING_BYTES
                    fields += [(b, player, 1), (b + 0x06, rng.choice((31, 47, 48, 50, 33)), 1),
                               (b + 0x36, rng.randrange(4), 1)]
        else:
            fields.append((ec_tile(x, y) + 6, rng.choice((5, 6, 6, 48)), 1))
            fields.append((MAP_SIZE, EC_MAP, 4))
            for d in range(6):  # some shore along the six directions
                for k in range(1, 9):
                    if rng.random() < 0.08:
                        fields.append((ec_tile(x + k * (d % 3 - 1), y + k * (d % 2)) + 6, 48, 1))
            chain = s
            for _ in range(8):  # the passengers' chain (valid settlers)
                nxt = rng.randrange(1, EC_SETTLERS)
                fields.append((SETTLERS + chain * SETTLER_BYTES + 0x10, nxt, 2))
                chain = nxt
        return [player, ec_hi(s), ec_lo(s)]
    return generate


def ec_promote(rng, fields):
    player = ec_player(rng)
    p = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    fields += [(p + 0x90, rng.randrange(120), 4), (p + 0xb4, rng.randrange(80), 4)]
    fields += [(p + 0xb8 + 4 * k, rng.choice((0, 0, 1, 1, 2)), 4) for k in range(3)]
    return [player, rng.choice((1, 2, 3, 0, 4, 0x81, 0x82, 0x83, 0x80, 0xFF))]


def ec_hold_position(rng, fields):
    player, group = rng.randrange(4), rng.randrange(10)
    index = player * 10 + group
    length = rng.randrange(8) if rng.random() < 0.9 else 0
    fields.append((PLAYER_LIST_LENGTHS + index * 4, length, 4))
    for i in range(length):
        s = ec_settler(rng, fields)
        fields.append((PLAYER_LISTS + (index * 100 + i) * 2, s, 2))
    return [player, group]


def ec_storehouse(rng, fields):
    n = ec_building(rng, fields)
    if rng.random() < 0.6:
        fields.append((BUILDINGS + n * BUILDING_BYTES + 6, 1, 1))
    return [ec_hi(n), ec_lo(n), ec_player(rng), rng.randrange(40)]


def ec_site_priority(rng, fields):
    n = ec_building(rng, fields)
    return [ec_hi(n), ec_lo(n), ec_player(rng)]


EC_COMMANDS.update({
    0x01: ec_place_building, 0x02: ec_scroll, 0x03: ec_settler_group, 0x04: ec_building_working,
    0x05: ec_work_area, 0x07: ec_goods_priority, 0x08: ec_area_setting(36), 0x11: ec_area_setting(51),
    0x14: ec_dock, 0x15: ec_cast_spell, 0x16: ec_ship(False), 0x17: ec_dock, 0x18: ec_promote,
    0x19: ec_ship(True), 0x1b: ec_hold_position, 0x1c: ec_storehouse, 0xf1: lambda rng, fields: [],
    0xf5: ec_site_priority, 0xf6: ec_site_priority,
    # Ids without a handler.
    0x00: lambda rng, fields: [rng.randrange(256)], 0x1a: lambda rng, fields: [rng.randrange(256)],
    0xf3: lambda rng, fields: [rng.randrange(256)], 0x80: lambda rng, fields: [rng.randrange(256)],
})

# Network message types other than Command (1) and StepGrant (2): type ->
# generator(rng, fields) returning (byte 1, payload). Types without a
# handler are logged as invalid.
EC_NET_MESSAGES: dict = {
    t: (lambda rng, fields: (rng.randrange(256), [rng.randrange(256)])) for t in (0, 7, 9, 0x20, 0x21, 0x23, 0xFF)
}


def ec_choices() -> tuple:
    """The command ids and network message types to generate. The
    environment variable S3RE_EC_ONLY (e.g. "c9,cd,n3": c = command id, n =
    message type, hex) restricts them, for work on one part of the function."""
    only = os.environ.get("S3RE_EC_ONLY")
    commands, net_types = sorted(EC_COMMANDS), sorted(EC_NET_MESSAGES)
    # Commands with many paths come up more often (EC_WEIGHTS: id -> copies).
    commands = [c for c in commands for _ in range(EC_WEIGHTS.get(c, 1))]
    if only:
        wanted = [item.strip().lower() for item in only.split(",") if item.strip()]
        commands = [c for c in commands if f"c{c:x}" in wanted]
        net_types = [t for t in net_types if f"n{t:x}" in wanted]
    return commands, net_types


def ec_message(type_: int, byte1: int, payload: list) -> list:
    data = [type_, byte1, 0, 0] + list(payload)
    while len(data) % 4:
        data.append(0)
    data[2] = len(data)
    return [int.from_bytes(bytes(data[i:i + 4]), "little") for i in range(0, len(data), 4)]


def ec_queue(offset: int, ring: int, count: int) -> list:
    return [(offset, FAKE_VTABLE, 4), (offset + 0x14, WORLD_BASE + ring, 4),
            (offset + 0x18, EC_RING_DWORDS, 4), (offset + 0x1c, 0, 4), (offset + 0x20, count, 4),
            (offset + 0x24, count, 4), (offset + 0x28, WORLD_BASE + EC_EVENT, 4),
            (offset + 0x2c, WORLD_BASE + EC_EVENT + 0x20, 4), (offset + 0x30, 0, 4)]


def execute_commands_world(rng, pool, args):
    fields = [(BUILDING_COUNT, rng.randrange(EC_BUILDINGS + 1), 4), (MAP_SIZE, EC_MAP, 4),
              (LOCAL_PLAYER, rng.randrange(4), 4), (UI_SELECTION_MODE, rng.choice((0, 1, 2, 3, 4, 4, 5)), 4),
              (UI_PAGE_290, rng.randrange(5), 4), (UI_PAGE_2A0, rng.randrange(5), 4),
              (0x2d288, rng.randrange(2), 4), (0x2d28c, rng.randrange(2), 4), (REFRESH_UI, 0, 4)]
    step = 0 if rng.random() < 0.08 else rng.randrange(1, 1000)
    fields.append((LOGIC_THREAD + EC_STEP, step, 4))
    dwords: list = []
    commands, net_types = ec_choices()
    for _ in range(rng.randrange(5)):
        if net_types and (rng.random() < 0.1 or not commands):
            type_ = rng.choice(net_types)
            byte1, payload = EC_NET_MESSAGES[type_](rng, fields)
            dwords += ec_message(type_, byte1, payload)
        elif commands:
            command = rng.choice(commands)
            dwords += ec_message(1, command, EC_COMMANDS[command](rng, fields))
    dwords += [2 | 8 << 16, (step + rng.randrange(-2, 6)) & 0xFFFFFFFF]  # StepGrant
    fields += [(EC_FROM_RING + 4 * i, d, 4) for i, d in enumerate(dwords)]
    fields += [(LOGIC_THREAD + 0x3c4, WORLD_BASE + EC_TO_NET, 4), (LOGIC_THREAD + 0x3c8, WORLD_BASE + EC_FROM_NET, 4)]
    fields += ec_queue(EC_FROM_NET, EC_FROM_RING, len(dwords)) + ec_queue(EC_TO_NET, EC_TO_RING, 0)
    return fields


def execute_commands_globals(rng, pool, args):
    fields = [(CAMPAIGN_MISSION, 66 if rng.random() < 0.25 else rng.randrange(3), 4),
              (SELECTED_BUILDING, rng.randrange(1, 4) if rng.random() < 0.5 else rng.randrange(EC_BUILDINGS + 2), 4),
              (REVEAL_MAP, 0, 4), (EC_SET_EVENT_CALL, EC_SET_EVENT_STUB, 6)]
    for extra in EC_GLOBALS_EXTRA:
        fields += extra(rng, pool, args)
    return fields


# Callees probed: the simulation functions the commands call (verified on
# their own; recorded with their arguments). NeighbourX/Y, TileDistance and
# IsOnMap run (pure functions of their arguments and the map size).
EC_CALLEES = {
    0x0060d670: (0, "thiscall", "probe"), 0x0060d680: (0, "thiscall", "probe"),  # Enter/LeaveCriticalSection
    0x004d99e0: (5, "thiscall", "probe"), 0x004ff3f0: (0, "thiscall", "probe"), 0x0051b4d0: (0, "thiscall", "probe"),
    0x004dfb40: (2, "thiscall", "probe"), 0x004df490: (1, "thiscall", "probe"), 0x004df510: (1, "thiscall", "probe"),
    0x004df590: (1, "thiscall", "probe"), 0x004df610: (1, "thiscall", "probe"), 0x004df690: (1, "thiscall", "probe"),
    0x004df730: (1, "thiscall", "probe"), 0x004df7d0: (1, "thiscall", "probe"), 0x004df850: (1, "thiscall", "probe"),
    0x004dee20: (1, "thiscall", "probe"), 0x004deec0: (1, "thiscall", "probe"), 0x004df8e0: (1, "thiscall", "probe"),
    0x00523840: (1, "thiscall", "probe"), 0x004ff3c0: (1, "thiscall", "probe"), 0x0051bc20: (2, "thiscall", "probe"),
    0x004dbb90: (1, "thiscall", "probe"), 0x004db710: (3, "thiscall", "probe"), 0x00508450: (1, "thiscall", "probe"),
    0x004fefd0: (10, "thiscall", "probe"), 0x0050ce80: (3, "thiscall", "probe"),
}

# Blocks only reachable through a C++ exception: the catch of Win32::CTimeout*
# (0x53b823; the probed wait never throws; in-process tests throw one).
# Dead: 0x53b81e, the `jmp` after the loop's `xor eax, eax; test eax, eax; je`.
EC_EXEMPT = [0x0053b823, 0x0053b82d, 0x0053b833, 0x0053b81e]
# Virtual calls (slot offset -> as Spec.virtuals); slot 0xc is the queue's wait.
EC_VIRTUALS = {0xc: 2}
# More S3.EXE globals per case: functions (rng, pool, args) -> [(address, value, size)].
EC_GLOBALS_EXTRA: list = []

SPECS += [
    Spec("CLogicThread_ExecuteCommands", 0x00535990, "logicthread", [], presets=execute_commands_world,
         globals=execute_commands_globals, virtuals=EC_VIRTUALS, callees=EC_CALLEES, exempt_blocks=EC_EXEMPT,
         # The epilogue (0x53b839, where the catch continues) is a separate
         # start in the function list; the jump tables follow at 0x53b850.
         cover_until=0x0053b850,
         # About 45 command ids and message types share the cases.
         case_scale=16),
]


# -- ExecuteCommands: SetTradeRoute 0x12, SetWorkshopProduction 0x13 (execute_commands_trade.cpp)
#
# Harbours (33) and marketplaces (34) with a Trade record 0..31 whose door,
# work tile and route tiles share walk regions / areas often; workshops (31,
# 47, 48, 50) and shipyards (32) idle or running. NeighbourX/Y run (pure).
# The UI's special menu (+0x2d298) is often on the trade (4..13) or
# workshop (14..16) pages.

EC_TRADE_RECORDS = 32
UI_SPECIAL_MENU = 0x2d298


def ec_trade_tile_box(fields: list, x: int, y: int, region: int, area: int, radius: int) -> None:
    for tx in range(x - radius, x + radius + 1):
        for ty in range(y - radius, y + radius + 1):
            if 0 <= tx < EC_MAP and 0 <= ty < EC_MAP:
                fields += [(ec_tile(tx, ty) + 0x10, region, 1), (ec_tile(tx, ty) + 0x14, area, 2)]


def ec_trade_building(rng, fields: list, types: tuple) -> int:
    n = rng.randrange(1, 4) if rng.random() < 0.5 else rng.randrange(EC_BUILDINGS + 1)
    b = BUILDINGS + n * BUILDING_BYTES
    fields += [(b + 0x06, rng.choice(types), 1), (b + 0x00, rng.randrange(4) if rng.random() < 0.9 else 0xFF, 1),
               (b + 0x21, 0 if rng.random() < 0.85 else rng.randrange(1, 256), 1)]
    fields.append((UI_SPECIAL_MENU, rng.randrange(18) if rng.random() < 0.8 else rng.getrandbits(32), 4))
    return n


def ec_trade_route(rng, fields):
    n = ec_trade_building(rng, fields, (33, 33, 34, 34, 32, 2))
    b = BUILDINGS + n * BUILDING_BYTES
    trade = rng.randrange(EC_TRADE_RECORDS)
    region, area = rng.randrange(1, 3), rng.randrange(3)
    door = (rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10))
    work = (rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10))
    x, y = rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10)
    fields += [(b + 0x34, trade, 2), (b + 0x0a, door[0], 2), (b + 0x0c, door[1], 2),
               (b + 0x30, work[0], 2), (b + 0x32, work[1], 2),
               (b + 0x22, 0xFFFF if rng.random() < 0.15 else rng.randrange(6), 2)]
    same = rng.random() < 0.7
    ec_trade_tile_box(fields, door[0], door[1], region, area, 0)
    ec_trade_tile_box(fields, work[0], work[1], region, area, 3)  # the water two steps from the work tile
    ec_trade_tile_box(fields, x, y, region if same else region + 1, area if same else area + 1,
                      1 if rng.random() < 0.5 else 0)
    if rng.random() < 0.1:
        fields.append((ec_tile(x, y) + 0x10, 0, 1))  # no walk region (kind 7, harbour)
    t = BUILDING_EXTRAS + trade * BUILDING_EXTRA_BYTES
    for point in range(4):
        if rng.random() < 0.5:  # the same tile, the same column, or anywhere
            px, py = rng.choice(((x, y), (x, y + 1), (rng.randrange(EC_MAP), rng.randrange(EC_MAP))))
            fields += [(t + 2 + 2 * point, px, 2), (t + 0x0a + 2 * point, py, 2)]
    for k in range(40):  # the offset table: growing distances (kind 7, harbour)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randrange(-4, 5) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randrange(-4, 5) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 2, 4)]
    kind = rng.randrange(13) if rng.random() < 0.9 else rng.randrange(256)
    if 8 <= kind <= 11:
        line = rng.randrange(34) if rng.random() < 0.9 else rng.randrange(40)
        if rng.random() < 0.5:
            fields.append((t + 0x12 + rng.randrange(36), rng.choice((0, 50, 99, 100, 255)), 1))
        return [ec_player(rng), kind, ec_hi(n), ec_lo(n), line, rng.choice((0, 1, 10, 60, 100, 255)), 0, 0]
    return [ec_player(rng), kind, ec_hi(n), ec_lo(n), ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y)]


def ec_workshop_production(rng, fields):
    n = ec_trade_building(rng, fields, (31, 47, 48, 50, 32, 32, 33))
    b = BUILDINGS + n * BUILDING_BYTES
    player = ec_player(rng)
    race = rng.randrange(4)
    fields += [(PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 4, race, 4),
               (b + 0x00, player if rng.random() < 0.8 else rng.randrange(4), 1),
               (b + 0x1f, rng.choice((0, 0, 0x10, 0x21, 0x32, 0x40, 0x03)), 1),
               (b + 0x20, 0xFF if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x22, 0xFFFF if rng.random() < 0.2 else rng.randrange(6), 2)]
    value = 0 if rng.random() < 0.3 else (rng.choice((0x10, 0x20, 0x30, 0x40, 0x50, 0)) |
                                          (race if rng.random() < 0.8 else rng.randrange(16)))
    return [player, value, ec_hi(n), ec_lo(n)]


EC_COMMANDS.update({0x12: ec_trade_route, 0x13: ec_workshop_production})
EC_CALLEES.update({
    0x005236b0: (1, "thiscall", "probe"),  # CLogicThread::DrawBuildingRouteOverlay (verified)
    0x004dda10: (1, "thiscall", "probe"),  # CGameData::ReleaseBuildingExtraTiles (verified)
})


# -- ExecuteCommands: area settings, garrison, carriers (execute_commands_areas.cpp)
# Commands 0x0a ChangeTransportOrder, 0x0b ChangeToolSettings, 0x0c
# ChangeGarrison, 0x10 ConvertCarriers.

EC_SETTLER_GROUP_COUNTS = 0x7ad620  # g_settlerGroups.counts
EC_CURRENT_SETTLER_GROUP = 0x7dfd68  # g_currentSettlerGroup


def ec_owned_area(rng, fields: list, player: int) -> int:
    n = rng.randrange(16)
    owner = player if rng.random() < 0.85 else rng.choice((0xFF, (player + 1) % 4))
    fields.append((AREAS + n * AREA_BYTES, owner, 1))
    return n


def ec_transport_order(rng, fields):
    player = rng.randrange(4)
    area = ec_owned_area(rng, fields, player)
    base = AREAS + area * AREA_BYTES
    order = list(range(1, 37))
    rng.shuffle(order)
    for g in range(36):
        value = order[g] if rng.random() < 0.9 else rng.choice((0, 255, order[g]))
        fields += [(base + 0x1f6 + g, value, 1), (base + 0x21a + g, rng.randrange(3), 1)]
    fields.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 4, rng.randrange(5), 4))
    if rng.random() < 0.5:  # the local player looks at the transport page
        fields += [(LOCAL_PLAYER, player, 4), (UI_SELECTION_MODE, 2, 4), (UI_PAGE_290, rng.choice((4, 4, 3)), 4)]
    good =rng.randrange(256) if rng.random() < 0.05 else rng.randrange(36)
    return [good, rng.randrange(8), player, ec_hi(area), ec_lo(area)]


def ec_tool_settings(rng, fields):
    player = rng.randrange(4)
    area = ec_owned_area(rng, fields, player)
    base = AREAS + area * AREA_BYTES
    fields += [(base + 0x23e + k, 5 * rng.randrange(15), 1) for k in range(3)]
    fields += [(base + 0x242 + k, rng.randrange(101), 1) for k in range(3)]
    if rng.random() < 0.5:  # the local player looks at the player menu
        fields += [(LOCAL_PLAYER, player, 4), (UI_SELECTION_MODE, 3, 4), (UI_PAGE_2A0, rng.choice((2, 3, 1)), 4)]
    value =0 if rng.random() < 0.35 else rng.randrange(30 if rng.random() < 0.5 else 256)
    return [rng.randrange(12), value, player, ec_hi(area), ec_lo(area)]


EC_SOLDIER_TYPES = (5, 32, 35, 8, 30, 33, 15, 31, 34, 0, 40)


def ec_garrison(rng, fields):
    player = rng.randrange(4)
    n = rng.randrange(1, EC_BUILDINGS + 1)
    b = BUILDINGS + n * BUILDING_BYTES
    if rng.random() < 0.85:  # a tower or castle of the player
        kind = rng.choice((15, 16, 18))
        size = {15: 1, 16: 3, 18: 6}[kind]
        swords = rng.randrange(size + 1)
        pikes = rng.randrange(size - swords + 1)
        fields += [(b, player, 1), (b + 6, kind, 1), (b + 0x1a, swords, 1), (b + 0x20, pikes, 1),
                   (b + 0x1c, rng.randrange(5), 1)]
        # The garrison chain (settlers 100..106; an unknown type continues with settler 0's next).
        length = rng.randrange(7)
        fields.append((b + 8, 100 if length else 0, 2))
        for k in range(length):
            s = SETTLERS + (100 + k) * SETTLER_BYTES
            fields += [(s + 1, rng.choice(EC_SOLDIER_TYPES), 1), (s + 0x10, 0 if k + 1 == length else 101 + k, 2)]
        fields += [(SETTLERS + 0x10, 0 if rng.random() < 0.5 else 110, 2), (SETTLERS + 110 * SETTLER_BYTES + 1, 5, 1),
                   (SETTLERS + 110 * SETTLER_BYTES + 0x10, 0, 2)]
    else:  # another player's building (the original keeps the sizes of an earlier command for the player's own)
        fields.append((b, (player + 1) % 4, 1))
    return [ec_hi(n), ec_lo(n), player, rng.randrange(10)]


def ec_convert_carriers(rng, fields):
    player = rng.randrange(4)
    kind = rng.randrange(6)
    x, y = rng.randrange(40, EC_MAP - 40), rng.randrange(40, EC_MAP - 40)
    for i in range(3000):
        dx, dy = (rng.randrange(-6, 7), rng.randrange(-6, 7)) if i < 40 else (0, 0)
        fields += [(NEIGHBOUR_OFFSETS + i * 16, dx & 0xFFFFFFFF, 4), (NEIGHBOUR_OFFSETS + i * 16 + 4, dy & 0xFFFFFFFF, 4)]
        if i < 40:
            t = ec_tile(x + dx, y + dy)
            s = rng.randrange(1, EC_SETTLERS)
            base = SETTLERS + s * SETTLER_BYTES
            fields += [(t, s if rng.random() < 0.8 else 0, 2), (t + 8, player if rng.random() < 0.85 else 0xFF, 1),
                       (base, player if rng.random() < 0.85 else 0xFF, 1),
                       (base + 1, rng.choice((0, 0, 40, 40, 22)), 1),
                       (base + 0x0e, rng.choice((1, 2, 0x37, 0x35, 3)), 2)]
    fields.append((ec_tile(x, y), 0, 2))  # offsets 40..2999 all look at (x, y): empty
    return [kind, player, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), rng.randrange(40 if rng.random() < 0.3 else 6)]


def ec_group_globals(rng, pool, args):
    return [(EC_SETTLER_GROUP_COUNTS, rng.randrange(50), 2), (EC_CURRENT_SETTLER_GROUP, rng.randrange(10), 4)]


EC_COMMANDS.update({0x0a: ec_transport_order, 0x0b: ec_tool_settings, 0x0c: ec_garrison, 0x10: ec_convert_carriers})
EC_CALLEES.update({
    0x0050cf50: (1, "thiscall", "probe"),  # ResetSettlerToIdle
    0x005428e0: (2, "thiscall", "probe"),  # CLogicThread::RemoveSettlerFromGroups
    0x00542990: (1, "thiscall", "probe"),  # CLogicThread::UpdateGroupMenu
})
EC_GLOBALS_EXTRA.append(ec_group_globals)


# -- ExecuteCommands: MoveUnits 0x09, 0x0d, 0x0e, 0x0f (execute_commands_units.cpp) --
#
# A group of up to 12 units of the player around a target; the tiles around
# the target hold enemy buildings / units for the attack search, the offset
# table has growing distances and small steps. SetSettlerTarget (0x503610)
# is probed; TileDistance runs.

EC_UNIT_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35, 22, 23, 36, 40, 41, 41, 41, 43, 44, 44, 0, 42)
EC_UNIT_JOBS = (212, 213, 302, 303, 55, 53, 142, 176, 0)


def ec_move_units(rng, fields):
    player, group = rng.randrange(4), rng.randrange(10)
    index = player * 10 + group
    x, y = rng.randrange(30, EC_MAP - 30), rng.randrange(30, EC_MAP - 30)
    if rng.random() < 0.05:
        x = rng.randrange(EC_MAP + 40)
    elif rng.random() < 0.1:  # next to the map's last row: offsets reach past it
        y = EC_MAP - rng.randrange(1, 4)
    fields.append((SETTLER_COUNT, EC_SETTLERS, 4))
    for p in range(20):
        base = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(base, 2 if rng.random() < 0.25 else 1, 4), (base + 4, rng.randrange(4), 4),
                   (base + 0x0c, rng.randrange(3), 4)]
    for k in range(400):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randrange(-8, 9) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randrange(-8, 9) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 6, 4)]
    # Tiles around the target: some settlers and buildings, terrain, regions.
    for dx in range(-24, 25):
        for dy in range(-24, 25):
            tx, ty = x + dx, y + dy
            if not (0 <= tx < EC_MAP and 0 <= ty < EC_MAP):
                continue
            t = ec_tile(tx, ty)
            fields += [(t, rng.randrange(1, EC_SETTLERS + 1) if rng.random() < 0.1 else 0, 2),
                       (t + 0x02, rng.randrange(1, EC_BUILDINGS + 1), 2),
                       (t + 0x06, rng.choice((5, 5, 6, 16, 32, 48, 0x11)), 1),
                       (t + 0x0a, 0x20 if rng.random() < 0.05 else 0, 1),
                       (t + 0x10, rng.randrange(2), 1), (t + 0x14, rng.randrange(2), 2),
                       (t + 0x16, rng.randrange(2), 2)]
    for n in range(1, EC_BUILDINGS + 1):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, rng.randrange(5), 1), (b + 0x06, rng.choice((15, 16, 18, 2, 33)), 1),
                   (b + 0x02, rng.randrange(EC_MAP), 2), (b + 0x04, rng.randrange(EC_MAP), 2)]
    catapults = rng.random() < 0.3
    for n in range(1, EC_SETTLERS + 1):
        s = SETTLERS + n * SETTLER_BYTES
        fields += [(s, rng.randrange(5), 1), (s + 0x01, rng.choice(EC_UNIT_TYPES), 1),
                   (s + 0x04, rng.randrange(EC_MAP), 2), (s + 0x06, rng.randrange(EC_MAP), 2),
                   (s + 0x0e, rng.choice(EC_UNIT_JOBS), 2),
                   (s + 0x1a, 0 if rng.random() < 0.7 else 1, 1),
                   (s + 0x1c, 0xFFFF if rng.random() < 0.5 else rng.randrange(EC_MAP), 4),
                   (s + 0x24, rng.randrange(EC_MAP), 2), (s + 0x26, rng.randrange(EC_MAP), 2)]
        fields += [(s + 0x30 + 2 * c, 0xFFFF if rng.random() < 0.5 else rng.randrange(EC_MAP), 2)
                   for c in range(6)]
    # The unit's own tiles must be on the map: their regions are compared.
    length = 0 if rng.random() < 0.1 else rng.randrange(13)
    fields.append((PLAYER_LIST_LENGTHS + index * 4, length, 4))
    for i in range(length):
        n = rng.randrange(1, EC_SETTLERS + 1) if rng.random() < 0.95 else EC_SETTLERS + 1 + rng.randrange(4)
        fields.append((PLAYER_LISTS + (index * 100 + i) * 2, n, 2))
        if n <= EC_SETTLERS:
            s = SETTLERS + n * SETTLER_BYTES
            fields += [(s, player if rng.random() < 0.9 else rng.randrange(4), 1),
                       (s + 0x01, 41 if catapults and rng.random() < 0.94 else rng.choice(EC_UNIT_TYPES), 1),
                       (s + 0x04, x + rng.randrange(-20, 21), 2), (s + 0x06, y + rng.randrange(-20, 21), 2)]
    return [player, group, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y)]


EC_COMMANDS.update({0x09: ec_move_units, 0x0d: ec_move_units, 0x0e: ec_move_units, 0x0f: ec_move_units})
EC_CALLEES.update({0x00503610: (3, "thiscall", "probe")})  # SetSettlerTarget
# 0x536faa: the clamp of the offset index to 9900, dead (the loop stops at 5000).
EC_EXEMPT.extend([0x00536faa])


# -- ExecuteCommands: DestroyBuilding, multiplayer / UI commands, network
# messages (execute_commands_net.cpp) ------------------------------------------
#
# The texts run as they are: sprintf, std::string, the wide-string helpers
# and the UI's message list (0x51e440, writing the thread's list at +0x414,
# 8 entries here, emptied per case). Probed: the graphics text lookup
# (0x5faaa0, returns the narrow text "Desync!" 0x73021c, which has no
# conversions), CrushBuilding, the game-over / player-list helpers, the
# multiplayer save functions, the world dump, CComputerPlayer's constructor
# (sometimes 0, to reach the assertion, which is probed too), and the window
# message of FilmEnd. Checksums and stamps of SyncCheck are chosen together
# with S3.EXE's checksum table (EC_NET_PENDING_GLOBALS, set by the message
# generators and written by the globals function of the same case).

EC_NET_PENDING_GLOBALS: list = []
EC_SYNC_CHECKSUMS = 0x7deb58  # g_syncChecksums[20][8]
EC_SIMULATION_PAUSED = 0x7ace62
EC_SYNC_RANDOM_HISTORY = 0x7dfb58
EC_GAME_TICK = 0x7dfd48
EC_UI_LIST = 0x414          # CLogicThread: the UI message list (0x1c per entry)
EC_UI_LIST_COUNT = 0x644
EC_UI_LIST_INDEX = 0x648


EC_PLAYER_SETUP = SCRATCH + 5 * SCRATCH_SLOT_BYTES  # what the probed 0x4351e0 returns


def ec_net_state(rng, fields: list) -> int:
    """Players, objectives, save state, the thread's UI message list, the
    UI feedback ring (PushUiFeedback) and the player setup object 0x5db260
    copies the (empty) name from. Returns the number of active players."""
    fields += [(0x15394ac, rng.randrange(10), 4), (EC_PLAYER_SETUP + 0x18, 0, 4), (EC_PLAYER_SETUP + 0x1c, 0, 4), (EC_PLAYER_SETUP + 0x20, 0, 4),
               (EC_PLAYER_SETUP + 0x24, 0, 4)]
    fields += [(LOGIC_THREAD + EC_UI_LIST_COUNT, 8, 4), (LOGIC_THREAD + EC_UI_LIST_INDEX, rng.randrange(8), 4)]
    for k in range(8):
        entry = LOGIC_THREAD + EC_UI_LIST + k * 0x1c
        fields += [(entry + 4, 0, 4), (entry + 8, 0, 4), (entry + 0xc, 0, 4)]
    active = 0
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        state = 1 if rng.random() < 0.6 else rng.randrange(3)
        active += state == 1
        fields += [(record, state, 4),
                   (record + 0x1c, (p + 3) % 20 if rng.random() < 0.8 else rng.randrange(20), 4)]
    fields += [(0x1195dcc, rng.randrange(2), 1), (0x2d250, rng.randrange(2), 1), (0x15394b0, rng.randrange(2), 1),
               (LOGIC_THREAD + 0x224, 0 if rng.random() < 0.3 else rng.randrange(4000, 6000), 4),
               (LOGIC_THREAD + 0x228, rng.randrange(3), 4), (LOGIC_THREAD + 0x22c, rng.randrange(6), 4),
               (0xe115dc, rng.randrange(1, 60), 4), (0x110a578, rng.randrange(1, 60), 4),
               (0x1238858, rng.randrange(1, 60), 4), (SETTLER_COUNT, rng.randrange(1, 60), 4)]
    return active


def ec_net_player(rng) -> int:
    return rng.randrange(20) if rng.random() < 0.15 else rng.randrange(4)


def ec_net_simple(rng, fields):
    ec_net_state(rng, fields)
    return [ec_net_player(rng)]


def ec_save_done(rng, fields):
    active = ec_net_state(rng, fields)
    if rng.random() < 0.6:  # the last missing acknowledgement
        fields.append((LOGIC_THREAD + 0x22c, active - 1, 4))
    return [ec_net_player(rng)]


def ec_destroy_building(rng, fields):
    ec_net_state(rng, fields)
    n = ec_building(rng, fields)
    b = BUILDINGS + n * BUILDING_BYTES
    fields.append((b + 6, rng.choice((41, 43, 45, 55, 15, 2)), 1))
    return [ec_hi(n), ec_lo(n), ec_net_player(rng)]


def ec_player_lost(rng, fields):
    ec_net_state(rng, fields)
    player = ec_net_player(rng)
    for n in range(1, EC_BUILDINGS + 1):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, player if rng.random() < 0.5 else rng.randrange(4), 1), (b + 6, rng.choice((15, 16, 18, 2)), 1)]
    return [player]


def ec_sync_check(rng, fields):
    ec_net_state(rng, fields)
    player = rng.randrange(4)
    table = [rng.randrange(4) for _ in range(7)]
    stamp_now = rng.choice((0, 5, 6))
    for k, value in enumerate(table):
        EC_NET_PENDING_GLOBALS.append((EC_SYNC_CHECKSUMS + (player * 8 + k) * 4, value, 4))
    EC_NET_PENDING_GLOBALS.append((EC_SYNC_CHECKSUMS + (player * 8 + 7) * 4, stamp_now, 4))
    EC_NET_PENDING_GLOBALS.append((EC_SIMULATION_PAUSED, 0 if rng.random() < 0.8 else 1, 2))
    payload = [player, rng.randrange(256)]
    for k in range(7):
        value = table[k] if rng.random() < 0.7 else rng.randrange(4)
        payload += list(value.to_bytes(4, "big"))
    stamp = rng.choice((stamp_now, stamp_now, stamp_now + 1, max(stamp_now - 1, 0), 0))
    return payload + list(stamp.to_bytes(4, "big"))


def ec_check_version(rng, fields):
    ec_net_state(rng, fields)
    index = rng.randrange(32)
    word = rng.randrange(4)
    EC_NET_PENDING_GLOBALS.append((EC_SYNC_RANDOM_HISTORY + index * 2, rng.randrange(4), 2))
    return [ec_hi(index), ec_lo(index), ec_hi(word), ec_lo(word), ec_net_player(rng)]


def ec_net_message(type_: int):
    def generate(rng, fields):
        ec_net_state(rng, fields)
        net_id = rng.randrange(22)
        if type_ in (3, 5):
            return net_id, []
        if type_ == 4:
            return 0, []
        if type_ == 6:  # a wide text at message byte 12
            return 0, [net_id, 1, 2, 3, 4, 5, 6, 7, ord("H"), 0, ord("i"), 0, 0, 0]
        if type_ == 8:
            return 0, [net_id, rng.randrange(256)]
        payload = [0] * (6 + 4 * 20 + 2)  # 0x22: lag words at message bytes 6 and 10 + 4 * id
        for at in range(2, len(payload) - 1, 4):
            lag = rng.choice((0, 4000, 5000, 5001, 60000))
            payload[at], payload[at + 1] = lag & 0xFF, lag >> 8
        return 0, payload
    return generate


def ec_net_globals(rng, pool, args):
    fields = [(EC_GAME_TICK, rng.randrange(5000, 10000), 4)] + EC_NET_PENDING_GLOBALS
    EC_NET_PENDING_GLOBALS.clear()
    return fields


EC_COMMANDS.update({
    0x06: ec_destroy_building, 0xf2: ec_net_simple, 0xf4: ec_sync_check, 0xf7: ec_net_simple,
    0xf8: ec_save_done, 0xf9: ec_net_simple, 0xfa: ec_net_simple, 0xfb: ec_check_version,
    0xfc: ec_player_lost, 0xfd: ec_net_simple, 0xfe: ec_net_simple, 0xff: ec_net_simple,
})
EC_NET_MESSAGES.update({t: ec_net_message(t) for t in (3, 4, 5, 6, 8, 0x22)})
EC_GLOBALS_EXTRA.append(ec_net_globals)
EC_CALLEES.update({
    0x005faaa0: (2, "thiscall", lambda h: 0x0073021c),   # CGraphicsData text lookup -> "Desync!"
    0x004d9250: (1, "thiscall", "probe"),                # CrushBuilding (verified)
    0x00540500: (1, "thiscall", "probe"), 0x005411b0: (0, "thiscall", "probe"),  # UI / players (not ours)
    0x0050ac40: (2, "thiscall", "probe"), 0x0050af70: (0, "thiscall", "probe"),  # multiplayer save
    0x0050b3c0: (0, "thiscall", "probe"),
    0x004ff6d0: (3, "thiscall", "probe"),                # desync dump file
    0x00550900: (4, "thiscall", lambda h: 0 if h % 8 == 0 else 0x5000 + (h >> 8) % 20),  # CComputerPlayer
    0x00610960: (1, "stdcall", "probe"),                 # Win32::ThrowOutOfMemory (fastcall text, file; line)
    0x0060f480: (3, "thiscall", "probe"),                # post a window message (FilmEnd)
    # The game setup's player (lobby code with vectors that reallocate): a fake object.
    0x004351e0: (1, "thiscall", lambda h: WORLD_BASE + EC_PLAYER_SETUP),
})
# Dead: the other branches of the std::string code the original inlines in
# 0x06 and 0xf7 (string(buffer) through two temporaries that share one
# buffer, then its destructor). The temporaries are released while the
# result still shares the buffer (count > 0), so their `delete` branches
# (0x53746a, 0x5374a3, 0x53a8e1, 0x53a91a) never run; when the result is
# destroyed its count is 0, so the unsharing (0x53750b, 0x53750f) and the
# count decrement (0x53758d, 0x537591) don't run either, and _Grow(0) in its
# erase returns false (0x537570).
EC_EXEMPT.extend([0x0053746a, 0x005374a3, 0x0053750b, 0x0053750f, 0x00537570, 0x0053758d, 0x00537591,
                  0x0053a8e1, 0x0053a91a])
_ec_spec = next(s for s in SPECS if s.ours == "CLogicThread_ExecuteCommands")
_ec_spec.new_fails = 0.15        # only the computer player's block (0x780 bytes) fails: 0x53afba, 0x53b201
_ec_spec.new_fails_size = 0x780

# The texts' CRT functions (sprintf, mbstowcs in 0x60b840, their locale
# locks) call InterlockedIncrement / InterlockedDecrement and
# Enter/LeaveCriticalSection, which the emulator doesn't implement: for the
# case, those import slots point at single-threaded equivalents in three
# padding gaps of S3.EXE (0x624aa1, 0x627ec2: `mov ecx, [esp+4]; inc/dec
# dword [ecx]; mov eax, [ecx]; ret 4`; 0x62c181: `ret 4`), and the CRT's
# _lock / _unlock (0x624a2b / 0x624a8c, cdecl) return at once (the first
# _lock of a lock number would allocate its critical section with HeapReAlloc).
EC_INTERLOCKED_INCREMENT = 0x00624aa1
EC_INTERLOCKED_DECREMENT = 0x00627ec2
EC_LOCK_NOTHING = 0x0062c181


def ec_interlocked_stubs(rng, pool, args):
    return [(EC_INTERLOCKED_INCREMENT, int.from_bytes(bytes.fromhex("8b4c2404ff018b01c20400"), "little"), 11),
            (EC_INTERLOCKED_DECREMENT, int.from_bytes(bytes.fromhex("8b4c2404ff098b01c20400"), "little"), 11),
            (EC_LOCK_NOTHING, int.from_bytes(bytes.fromhex("c20400"), "little"), 3),
            (0x00655244, EC_INTERLOCKED_INCREMENT, 4), (0x00655240, EC_INTERLOCKED_DECREMENT, 4),
            (0x006550fc, EC_LOCK_NOTHING, 4), (0x00655128, EC_LOCK_NOTHING, 4),  # import slots
            (0x00624a2b, 0xc3, 1), (0x00624a8c, 0xc3, 1)]  # _lock, _unlock: ret


EC_GLOBALS_EXTRA.append(ec_interlocked_stubs)
