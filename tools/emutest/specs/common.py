"""Emulator specs, part `common`: shared: the Spec class, argument generators and helpers used by several subsystems.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""
SPECS: list = []
SELF_TEST: list = []

import os
import random
import struct
from dataclasses import dataclass
from typing import Callable

# The game's executable, for helpers that read S3.EXE's bytes. The parts run
# in specs/__init__.py's namespace, so __file__ is tools/emutest/specs/
# __init__.py: the repository is four folders up (session 51; the former
# single specs.py was one folder higher).
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S3_EXE_PATH = os.path.join(REPO_ROOT, "Settlers3Ultimate", "S3.EXE")

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
    # object, not the strings built before it); a tuple: one of them per
    # case (session 55: the two notes of HOUSE_REQUEST_AGENT::TakeNote).
    new_fails_size: int | tuple[int, ...] | None = None
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


def record_counts(rng, pool, args):
    return [(BUILDING_COUNT, rng.randrange(40), 4), (SETTLER_COUNT, rng.randrange(40), 4)]


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


SCREEN_WIDTH = 0x730f18
SCREEN_HEIGHT = 0x730f1c


def screen_size(rng, pool, args):
    def size(high):
        return rng.randint(0, high) if rng.random() < 0.5 else rng.getrandbits(32)
    return [(SCREEN_WIDTH, size(2048), 4), (SCREEN_HEIGHT, size(1536), 4)]


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


def building_on_map(rng, pool, args):
    building = BUILDINGS + args[0] * BUILDING_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    return [(building + 0xa, x, 2), (building + 0xc, y, 2),
            (tile_area_offset(x, y), rng.randrange(AREA_LIMIT), 2)]


def diagonal_map_size(rng, pool, args):
    """Never 0 (a 2^32-iteration loop in both); 1..3 often (edge cases)."""
    size = rng.randint(1, 3) if rng.random() < 0.3 else rng.randint(1, 400)
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


PILE_KINDS = (0, 7, 8, 9, 0xfd, 0xfe, 0xff)


NEIGHBOUR_OFFSETS = 0x110a588


PENDING_RING = 0x2f990


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


PILE_CLAIMS = 0x1195dd4


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


TILE = between(2, 381)  # tiles at least 2 from the edge of a 384 map
AREA_HIGH_WATER = 0xe115dc
PLAYER_RACE = PLAYER_RECORDS + 4
LOCAL_PLAYER = 0x2d2d4


# --- batch 19 ---------------------------------------------------------------

SETTLER_HIGH_WATER = 0x12a5e5c


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


def string_bool(rng, pool) -> int:
    return rng.choice([0, 1, 0x100, 0x101, rng.getrandbits(32)])


STRING_SELF_PTR = WORLD_BASE + STRING_SELF
STRING_OTHER_PTR = WORLD_BASE + STRING_OTHER
STRING_TEXT_PTR = WORLD_BASE + STRING_TEXT
STRING_RESULT_PTR = WORLD_BASE + STRING_RESULT


def text_or_null(rng, pool) -> int:
    return 0 if rng.random() < 0.15 else STRING_TEXT_PTR

# --- Batch 22: the other std::string members, string members of game
# objects (src/s3re/game/framework/string_members.cpp) and std::logic_error /
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


def id_with_noise(limit: int) -> Generator:
    """An index below `limit`, sometimes with garbage in the upper half."""
    def generate(rng, pool):
        value = rng.randrange(limit)
        return value | (rng.getrandbits(16) << 16) if rng.random() < 0.3 else value
    return generate


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
UI_CHILD = UI_BASE + 0x380
UI_RECTS = WORLD_BASE + SCRATCH

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


def ui_spec(name, address, args, this=ELEMENT, **kwargs):
    """A thiscall UI member: ECX = `this`, EDX random, then `args`."""
    return Spec(name, address, "fastcall", [one_of(this), anything, *args], presets=ui_objects,
                virtuals=UI_VIRTUALS, **kwargs)


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


def small_or_anything(low, high):
    return lambda rng, pool: (rng.randint(low, high) if rng.random() < 0.8 else rng.getrandbits(32)) & 0xFFFFFFFF
SETTLERS = 0x12a5e60

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
# std::_Lockit of S3.EXE's MSVC6 library (constructor 0x61e6e3 enters,
# destructor 0x61e77f leaves the critical section of the std::_Tree nil
# nodes; msvc6/lockit.h). The emulator has no threads and no critical
# sections (the first call would also run the CRT's lock set-up and
# atexit), so the calls are recorded as probes and their order compared:
# replacements of tree code must lock and unlock exactly where the
# original does. "cdecl" with no arguments: the lock object's address
# (ECX, a stack local of each frame) is not recorded; EAX is not used.
LOCKIT_PROBES = {0x0061e6e3: (0, "cdecl", "probe"), 0x0061e77f: (0, "cdecl", "probe")}


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
        path = S3_EXE_PATH
        pe = pefile.PE(path, fast_load=True)
        data = pe.get_data(0x6a8060 - 0x400000, 4 * 0x800)
        _ANIMATION_TABLE.extend(struct.unpack("<2048I", data))
    return _ANIMATION_TABLE


def handle_world(rng, pool, args):
    return [(4, rng.choice([0, 0, rng.getrandbits(32)]), 4), (8, rng.choice([1, 1, 0, rng.getrandbits(32)]), 4)]

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


def coordinate(rng, pool) -> int:
    return rng.randrange(7000)


EFFECT_TABLE_PADDING = tuple(slot * 0x4c + p for slot in range(1, 65) for p in EFFECT_PADDING)


def spawn_spec(name, address, args, **kwargs):
    return Spec(name, address, "thiscall", args, presets=effect_table(0.0), ignore_bytes=EFFECT_TABLE_PADDING,
                max_cases=150, **kwargs)


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


# -- batch 27: Lobby::CMemoryBlock and CLMGeneric (lobby_memory_block.cpp) ---
#
# `this` is a block at world offset 0 (block functions) or a message at 0
# whose block is at +4 (CLMGeneric). The other block / message is in scratch
# slot 0; data blocks are in slots 1 and 2 (lazily random bytes).

BLOCK_OTHER = SCRATCH
BLOCK_DATA = SCRATCH + 1 * SCRATCH_SLOT_BYTES
BLOCK_OTHER_DATA = SCRATCH + 2 * SCRATCH_SLOT_BYTES


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
SITE_OFFSETS = 0x110a588


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
LOCAL_PLAYER = 0x2d2d4


# Package A, batch 2: the pathfinder's walking helpers (pathing.cpp). The
# settler argument points at settler record n; the state names the same n.
DIR_X = (1, 1, 0, -1, -1, 0)
DIR_Y = (0, 1, 1, 0, -1, -1)


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


def chain(base, size, next_offset, prev_offset, members):
    fields = []
    for k, n in enumerate(members):
        r = base + n * size
        fields += [(r + prev_offset, members[k - 1] if k else 0, 2),
                   (r + next_offset, members[k + 1] if k + 1 < len(members) else 0, 2)]
    return fields, (members[0] if members else 0)


def pile_on_tile(rng, pile, area_limit=AREA_LIMIT):
    """A pile record with a position on the map and an area below the limit."""
    record = PILES + pile * PILE_BYTES
    x, y = rng.randrange(5, 379), rng.randrange(5, 379)
    return [(record + 0x6, x, 2), (record + 0x8, y, 2), (tile_area_offset(x, y), rng.randrange(area_limit), 2),
            (record + 0xa, rng.randrange(0x1000), 2), (record + 0xc, rng.randrange(0x1000), 2)]
TALLIES = 0x007acfd4
CAMPAIGN_MISSION = 0x007dfdb0


# --- package D batch 1: fog of war, map effect expiry, traffic, spell cost --

SIGHT_MAP = 0x2d2d0
LOCAL_PLAYER = 0x2d2d4
REVEAL_COUNT = 0x14d28a4
REVEAL_POINTS = 0x14d28a8
PLAYER_LIST_LENGTHS = 0x11940bc
AREAS = 0xe145e8
AREA_BYTES = 0x2e0
AREA_HIGH_WATER = 0xe115dc
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


def _small(n, zero=0.5):
    return lambda h: 0 if (h & 0xFF) < int(256 * zero) else 1 + (h >> 8) % n


def _write(machine, offset, value, size):
    machine.guest_write(WORLD_BASE + offset, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))
TILE_RING_INDEX = 0xe07888
CAMPAIGN_MISSION = 0x7dfdb0
PLAYER_LIST_LENGTHS = 0x11940bc
