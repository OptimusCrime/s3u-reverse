"""Emulator specs, part `map`: specs of the replacements in src/s3re/game/map/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def tile_area_of_args(rng, pool, args):
    return [(tile_area_offset(args[0], args[1]), rng.randrange(AREA_LIMIT), 2)]


def slice_map_size(rng, pool, args):
    """Mostly small maps: a slice clears size * size / 8 bytes."""
    r = rng.random()
    size = rng.randint(1, 3) if r < 0.2 else rng.randint(1, 48) if r < 0.9 else rng.randint(1, 400)
    return [(MAP_SIZE, size, 4)]


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

SPECS += [
    # directions (map_directions.cpp)
    Spec("NeighbourX", 0x00503700, "stdcall", [anything, below(8, 0.2)]),
    Spec("NeighbourY", 0x00503730, "stdcall", [anything, below(8, 0.2)]),
    Spec("PreviousDirection", 0x005038a0, "stdcall", [below(8, 0.2)]),
    Spec("NextDirection", 0x005038c0, "stdcall", [below(8, 0.2)]),
    Spec("DirectionFromDelta", 0x005037f0, "stdcall", [between(-20, 20), between(-20, 20)]),
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
    # batch 14: region merges, area flood, requests, placement, terrain passes
    Spec("CGameData_FloodMoveArea", 0x004e62c0, "thiscall", [below(12), below(12), between(1, 6)],
         presets=flood_area, max_cases=150),
    Spec("CGameData_MergeTileWord16Regions", 0x0051c5e0, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, False), max_cases=500),
    Spec("CGameData_MergeTileByte10Regions", 0x0051ca00, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, True), max_cases=500),
    Spec("CGameData_GrowTerrain", 0x00519470, "thiscall", [below(4), below(4), between(-5, 110)],
         presets=small_map_terrain, max_cases=600),
    Spec("CGameData_UpdateAllBorderFlags", 0x0051a420, "thiscall", [], presets=small_territory,
         max_cases=400),
    # batch 15
    Spec("CGameData_AssignWord16RegionsAround", 0x0051c0f0, "thiscall", [between(3, 12), between(3, 12)],
         presets=region_assignment, max_cases=400),
    Spec("CGameData_SplitTileWord16Region", 0x0051c1f0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, 0.1)),
    Spec("CGameData_SplitTileByte10Region", 0x0051caf0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, 0.1)),
    Spec("CGameData_SplitWord16RegionsAround", 0x0051c080, "thiscall", [between(1, 10), between(1, 10)],
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, True), max_cases=200),
]


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
    Spec("CGameData_UnblockTile", 0x0051b770, "thiscall", [between(3, 8), between(3, 8)], presets=unblock_tile,
         max_cases=300),
    Spec("CGameData_SetOwnerOfFreeTile", 0x0051b5d0, "thiscall",
         [between(1, 8), between(1, 8), lambda rng, pool: rng.choice([0, 1, 2, 0xff, 0x100, rng.getrandbits(32)])],
         presets=free_tile_owner, max_cases=400),
    Spec("CGameData_MergeAreaContents", 0x004e5a20, "thiscall", two_areas(), eax_mask=0,
         presets=merge_area_contents, max_cases=300),
    Spec("CGameData_JoinAreasAtTile", 0x004e6150, "thiscall", [between(1, 8), between(1, 8)], eax_mask=0,
         presets=join_areas, max_cases=400),
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


def shade_heights(rng, pool, args):
    """Heights of the tile and its neighbours: near each other (all branches
    of the difference mapping) or anywhere (clamping)."""
    x, y = args
    wide = rng.random() < 0.25
    base = rng.randrange(256)
    return [(tile(x + dx, y + dy) + 0x4, rng.randrange(256) if wide else (base + rng.randrange(25)) & 0xFF, 1)
            for dx in (-1, 0, 1) for dy in (-1, 0, 1)]

SPECS += [
    Spec("CGameData_TileShade", 0x00519720, "thiscall", [between(1, 380), between(1, 380)], presets=shade_heights),
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

SPECS += [
    Spec("CGameData_BuildRegionsAndAreas", 0x0051a580, "thiscall", [], presets=region_map, max_cases=60,
         max_instructions=200_000_000),
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

SPECS += [
    Spec("CGameData_UpdateShadeAround", 0x00519670, "thiscall", [between(2, 380), between(2, 380)],
         eax_mask=0, presets=shade_around_heights, max_cases=1000),
    Spec("CGameData_MakeFieldTile", 0x0051ba00, "thiscall", [between(1, 380), between(1, 380)],
         eax_mask=0, presets=terrain16_patch, max_cases=2000),
]


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

SPECS += [
    Spec("CGameData_ResetCivilianOnTile", 0x0050e980, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
    Spec("CGameData_LosePileOnTile", 0x00504f70, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
    Spec("CGameData_ClearTileOwner", 0x0051b640, "thiscall", [between(10, 370), between(10, 370)], eax_mask=0,
         presets=owned_tile_world, callees=SETTLER_PROBES, max_cases=1000),
]


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
    Spec("CGameData_RecalculateTerritory", 0x00519a90, "thiscall", [territory_args, territory_args],
         presets=territory_world, max_cases=25, max_instructions=400_000_000,
         callees={0x004d97b0: (2, "thiscall", "probe"), 0x0051b5d0: (3, "thiscall", "probe"),
                  0x0051b640: (2, "thiscall", "probe")}),
]


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


# -- load_map.cpp (session 53) ---------------------------------------------------

def loaded_terrain_map(rng, pool, args):
    """Maps of size 0..3 (no bands) or 4..24: patches of water variants 0..15,
    shore 0x30 and land, some random bytes."""
    n = rng.randrange(4) if rng.random() < 0.1 else rng.randint(4, 24)
    patch = rng.randint(2, 6)
    fields = [(MAP_SIZE, n, 4)]
    for x in range(n):
        for y in range(n):
            kind = ((x // patch) * 7 + (y // patch) * 3) % 5
            if rng.random() < 0.05:
                terrain = rng.getrandbits(8)
            elif kind < 2:
                terrain = rng.randrange(16)
            elif kind == 2:
                terrain = 0x30
            else:
                terrain = rng.randrange(0x10, 0x40)
            fields.append((tile(x, y) + 0x6, terrain, 1))
    return fields

SPECS += [
    Spec("CGameData_PrepareLoadedTerrain", 0x00519540, "thiscall", [], presets=loaded_terrain_map, max_cases=200),
]


# -- player_towns.cpp (session 53) ------------------------------------------------
def created_settler(machine, ecx, args, h):
    """CreateSettler's effect: a settler record at (x, y) (PlaceTown reads
    its position back)."""
    settler = 1 + h % 0x7cfe
    _write(machine, SETTLERS + settler * SETTLER_BYTES + 4, args[0], 2)
    _write(machine, SETTLERS + settler * SETTLER_BYTES + 6, args[1], 2)
    return settler


# PlaceTown: every callee is probed (calls and arguments compared); the
# start-goods getter returns 0..4 (low, medium, high and the map's own),
# AddBuildingSite and CreateSettler return record numbers.
PLACE_TOWN_PROBES = {
    0x00412300: (0, "thiscall", lambda h: h % 5),               # GameSettings::StartGoods
    0x004dbc50: (1, "thiscall", "probe"),                       # BuildingDone
    0x004dd280: (4, "thiscall", lambda h: 1 + h % 3998),        # AddBuildingSite
    0x005046b0: (4, "thiscall", "probe"),                       # DropGoodNear
    0x0050c9e0: (4, "thiscall", "probe", created_settler),       # CreateSettler
    0x0050cb20: (5, "thiscall", "probe"),                       # SpawnSettlersAround
    0x00519670: (2, "thiscall", "probe"),                       # UpdateShadeAround
    0x00519a90: (2, "thiscall", "probe"),                       # RecalculateTerritory
    0x0051b6f0: (2, "thiscall", "probe"),                       # BlockTile
    0x0051b7f0: (2, "thiscall", "probe"),                       # WearPath
}


def place_town_state(rng, pool, args):
    player = args[0]
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    return [(record, rng.choice([0, 1, 2, 2, rng.getrandbits(32)]), 4), (record + 4, rng.randrange(4), 4)]

SPECS += [
    Spec("CGameData_PlaceTown", 0x00506cf0, "thiscall", [below(20), between(16, 368), between(16, 368)],
         eax_mask=0, presets=place_town_state, callees=PLACE_TOWN_PROBES, max_cases=300),
]


# AddPlayerTowns: IsGoodStartPosition, IsFreeSmallTowerSite and PlaceTown
# are probed (a third of the site checks succeed); Random, TileDistance and
# FindPlayerByField0C run. Players 0..5 take part or not, with teams 0..3,
# placed or not, with or without a start position. A start position on the
# diagonal with the offset entries 0..9999 at (0, 0) never passes the
# mirror distance: the "keine Kreisposition" paths.
RANDOM_MAP_SYMMETRY = 0x2d274
ADD_TOWNS_PROBES = {
    0x005081a0: (4, "thiscall", lambda h: 1 if h % 3 == 0 else 0),  # IsGoodStartPosition
    0x00508420: (3, "thiscall", lambda h: 1 if h % 3 == 0 else 0),  # IsFreeSmallTowerSite
    0x00506cf0: (3, "thiscall", "probe"),                             # PlaceTown
}


def add_towns_state(rng, pool, args):
    size = rng.choice([128, 256, 384, 384, rng.randrange(100, 769)])
    symmetry = rng.choice([0, 1, 2, 3, 3, rng.choice([4, rng.getrandbits(32)])])
    fields = [(MAP_SIZE, size, 4), (RANDOM_MAP_SYMMETRY, symmetry, 4)]
    fields += spiral_offsets(400)
    stuck = rng.random() < 0.15
    mirrors = rng.random() < 0.7  # otherwise every team is 0: no mirror players
    if stuck:
        for k in range(400, 10000):
            fields += [(NEIGHBOUR_OFFSETS + k * 16, 0, 4), (NEIGHBOUR_OFFSETS + k * 16 + 4, 0, 4)]
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        active = rng.choice([1, 2, 0, rng.getrandbits(32)]) if p < 6 else 0
        if stuck:
            start = rng.randrange(size)
            sx, sy = start, start
        elif rng.random() < 0.5:
            sx, sy = 0xFFFFFFFF, rng.getrandbits(32)
        else:
            sx, sy = rng.randrange(size), rng.randrange(size)
        fields += [(record, active, 4), (record + 0x18, 0 if rng.random() < 0.85 else 1, 4),
                   (record + 0x0c, 0 if not mirrors else rng.randrange(4) if rng.random() < 0.9 else rng.getrandbits(32), 4),
                   (record + 0x10, sx, 4), (record + 0x14, sy, 4)]
    return fields

SPECS += [
    # Not covered: 0x507d72, 0x50778e and 0x5079cb, the "keine Zufallsposition" paths after 1,000,000
    # failed random attempts (several hundred million instructions); their continuation (DebugLog, the
    # player taken out) is the same code as the covered "keine Kreisposition" paths. 0x5075cc is dead:
    # symmetry 0 reaches it after a placement only if the attempt counter were 1,000,000.
    Spec("CGameData_AddPlayerTowns", 0x005074b0, "thiscall", [], presets=add_towns_state,
         callees=ADD_TOWNS_PROBES, max_cases=600, exempt_blocks=(0x507d72, 0x50778e, 0x5079cb, 0x5075cc)),
]


# -- session 54 (logic-misc): BuildMinimap (minimap.cpp), RebuildFogOfWar (fog_of_war.cpp),
#    InitTileShadesAndMapEdge (map_terrain.cpp) --

def build_minimap_world(rng, pool, args):
    return [(MAP_SIZE, rng.randrange(16, 385), 4)]


def build_minimap_globals(rng, pool, args):
    """Minimap widths 1..24 (every pixel probed), sometimes 0 or below (one
    divides by zero in both, so only -1 and less)."""
    width = rng.randrange(1, 25) if rng.random() < 0.85 else (-1 - rng.randrange(3)) & 0xFFFFFFFF
    return [(0x00730f28, width, 4)]


def init_shades_world(rng, pool, args):
    return [(MAP_SIZE, rng.choice([1, 2, 3]) if rng.random() < 0.15 else rng.randrange(4, 30), 4)]


SPECS += [
    # MinimapPixel is probed (its own spec covers it; here every pixel's call
    # and the stored AX are compared).
    Spec("CGameData_BuildMinimap", 0x004fb650, "thiscall", [], presets=build_minimap_world,
         globals=build_minimap_globals, max_cases=300, callees={0x004fb720: (4, "thiscall", "probe")}),
    # The five steps loop over whole record tables and have their own specs:
    # probed, so the order and arguments of the 57 calls are compared.
    Spec("CGameData_RebuildFogOfWar", 0x00518e70, "thiscall", [], eax_mask=0, max_cases=50,
         callees={0x00518ec0: (1, "thiscall", "probe"), 0x00518f20: (0, "thiscall", "probe"),
                  0x00519fa0: (1, "thiscall", "probe"), 0x0051a150: (1, "thiscall", "probe"),
                  0x00519010: (1, "thiscall", "probe")}),
    # TileShade runs; UpdateAllBorderFlags (whole map, own spec) is probed.
    Spec("CGameData_InitTileShadesAndMapEdge", 0x00519e80, "thiscall", [], eax_mask=0, presets=init_shades_world,
         max_cases=200, callees={0x0051a420: (0, "thiscall", "probe")}),
]
