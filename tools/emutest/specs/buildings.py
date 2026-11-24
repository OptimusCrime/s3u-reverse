"""Emulator specs, part `buildings`: specs of the replacements in src/s3re/game/buildings/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


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


def input_goods(rng, pool, args):
    """A building of a random type, often a construction site."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    kind = rng.randrange(0x50) if rng.random() < 0.95 else rng.randrange(256)
    return [(record + 0x6, kind, 1),
            (record + 0x21, rng.randrange(1, 256) if rng.random() < 0.3 else 0, 1)]


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

SPECS += [
    # building request flags (building_flags.cpp)
    *[Spec(f"CGameData_{kind}BuildingFlag{bit}", address, "thiscall", [between(1, 2000)],
           eax_mask=0, presets=building_request_state)
      for kind, bit, address in [
          ("Clear", "02", 0x004df490), ("Clear", "04", 0x004df510),
          ("Clear", "08", 0x004df590), ("Clear", "10", 0x004df610),
          ("Set", "02", 0x004df690), ("Set", "04", 0x004df730),
          ("Set", "08", 0x004df7d0), ("Set", "10", 0x004df850)]],
    # building/settler tile references (building_tiles.cpp)
    Spec("CGameData_RemoveBuildingFromTiles", 0x004d9940, "thiscall", [between(0, 2000)],
         eax_mask=0, presets=building_arg_on_map),
    Spec("CGameData_LeaveBuildingSlot", 0x0050cc10, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("CGameData_MoveSettlerToBuildingTileA", 0x0050de90, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("CGameData_MoveSettlerToBuildingTileB", 0x0050df40, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    # building input goods (building_goods.cpp)
    Spec("CGameData_BuildingInputWord2A", 0x004df150, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("CGameData_BuildingInputWord24", 0x004df220, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("CGameData_SetBuildingInputWord2A", 0x004df2f0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    Spec("CGameData_SetBuildingInputWord24", 0x004df3c0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    Spec("CGameData_AddBuildingGoodsRequest", 0x004ddf30, "thiscall",
         [below(2000), below(36, 0.1), one_of(0, 1, 7, 0xFFFFFFFF), below(64)], presets=goods_request),
    Spec("CGameData_RemoveBuildingFromAreaRequests", 0x004def80, "thiscall", [between(1, 63)],
         presets=request_lists, max_cases=300),
]

SPECS += [
    Spec("CGameData_GoodsRequestPriority", 0x004ff380, "thiscall", [anything, anything]),
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
SITE_CACHE = 0x1196024
SITE_BUILDINGS = 0x1499e64
SITE_TYPES = [7, 8, 9, 0x1d, 0x25, 0x38, 0x20, 0x21, 1, 2, 3, 5, 0xe, 0xf, 0x18, 0x19, 0x1e, 0x24, 0x33]


def site_type(rng, pool) -> int:
    return rng.choice(SITE_TYPES) if rng.random() < 0.5 else rng.randint(1, 56)


def site_tile(x: int, y: int) -> int:
    return SITE_TILES + ((x * 0x300 + y) & 0xFFFFFFFF) * 24


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
    # UnblockTile (verified, own spec) is probed: on the lazily random map
    # its region floods would make the cases very slow; the calls and their
    # arguments are compared.
    Spec("CGameData_ClearBuildingFootprint", 0x0051aae0, "thiscall", [between(1, 0xf9f)], eax_mask=0,
         presets=footprint_world, callees={0x0051b770: (2, "thiscall", "probe")}, max_cases=400),
]


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
]
