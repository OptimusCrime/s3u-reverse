"""What tools/emutest/run.py tests: one Spec per replaced function.

A Spec names the original function (address in S3.EXE) and ours (the C++
function name in s3re.dll), the calling convention, how to generate each
stack argument, which bits of EAX must match, and optionally world fields
to pin per case (`presets`, e.g. a small map size so map-wide loops stay
fast in the emulator). Everything else in the world is lazily random (see
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


@dataclass
class Spec:
    ours: str            # C++ name of our replacement in s3re.dll
    original: int        # address in S3.EXE
    convention: str      # "thiscall" (ECX = world; ours is __fastcall) or "stdcall"
    args: list[Generator]
    eax_mask: int = 0xFFFFFFFF  # 0: EAX not compared (callers don't use it)
    # (rng, pool, args) -> [(world offset, value, size)]: fields pinned for the case.
    presets: Callable | None = None
    # Upper limit on cases (for functions that are slow to emulate).
    max_cases: int | None = None


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
]
