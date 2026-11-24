"""Emulator specs, part `mapgen`: specs of the replacements in src/s3re/game/mapgen/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


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

SPECS += [
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
]


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

SPECS += [
    Spec("CGameData_ReplaceTerrainNextTo", 0x00519270, "thiscall", [terrain_value, terrain_value, anything],
         eax_mask=0, presets=small_terrain_map, max_cases=300),
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


def create_world(rng, pool, args):
    return fresh_world_tables(600)(rng)

SPECS += [
    Spec("CGameData_ExpandTerrain", 0x00519440, "thiscall", [terrain_or_mark, terrain_or_mark],
         presets=small_terrain_map_with_marks, max_cases=300),
    Spec("CGameData_MarkTerrainEdge", 0x0051a2e0, "thiscall", [terrain_or_mark],
         presets=small_terrain_map_with_marks, max_cases=300),
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
