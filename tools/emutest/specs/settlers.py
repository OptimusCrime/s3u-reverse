"""Emulator specs, part `settlers`: specs of the replacements in src/s3re/game/settlers/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


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

SPECS += [
    # settlers (settlers.cpp)
    Spec("CGameData_CanSettlerEnterTile", 0x00503760, "thiscall",
         [below(384), below(384), below(20), below(48)], presets=full_map),
    Spec("CGameData_SettlerGroup", 0x005038e0, "thiscall", [below(4096)]),
    # settlers (settler_queries.cpp)
    Spec("CGameData_SetSettlerTarget", 0x00503610, "thiscall",
         [below(0x10000), below(400, 0.1), below(400, 0.1)], presets=map_size(between(0, 400))),
    Spec("CGameData_CountListSettlersNear", 0x0050e8f0, "thiscall",
         [lambda rng, pool: 0 if rng.random() < 0.1 else rng.randrange(1, 0x3000),
          below(20, 0.2), below(384), below(384), below(3, 0.1)],
         presets=settler_list),
    Spec("CGameData_FindNearbyOwnCatapult", 0x0050cdb0, "thiscall",
         [between(1, 0x2fff), below(384), below(384)], presets=settlers_around),
    Spec("CGameData_PlaceSettlerNear", 0x0050ce80, "thiscall",
         [below(0x3000), below(384, 0.03), below(384)], presets=place_settler, max_cases=400),
    Spec("CGameData_SendNearestSettlerToPile", 0x004ddbd0, "thiscall",
         [between(1, 0x800), below(20), anything], presets=pile_and_waiting),
    Spec("CGameData_SendNearestSettlerToBuilding", 0x004ddd70, "thiscall", [between(1, 0x800)],
         presets=building_and_waiting),
    Spec("CGameData_RemoveSettlerFromLists", 0x0050dff0, "thiscall", [between(1, 8)],
         presets=settler_lists_world, globals=settler_lists_globals, max_cases=500),
    Spec("CGameData_SendNearestWorkerToBuilding", 0x004db500, "thiscall",
         [between(1, 0x800), below(40), lambda rng, pool: rng.choice([rng.randrange(70), rng.getrandbits(32)])],
         presets=worker_and_list, globals=worker_globals, max_cases=1000),
]


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

SPECS += [
    Spec("CGameData_FetchGoodsToTradeBuilding", 0x004de0a0, "thiscall",
         [between(1, 0x40), between(1, 0x800), below(36)], eax_mask=0, presets=fetch_goods, max_cases=1000),
]
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

SPECS += [
    Spec("CGameData_SettlerStartHealth", 0x00510470, "thiscall", [below(LAST_SETTLER + 1)], presets=settler_health),
    Spec("CGameData_CreateSettler", 0x0050c9e0, "thiscall",
         [below(384), below(384), lambda rng, pool: rng.randrange(0x50) if rng.random() < 0.8 else anything(rng, pool),
          below(20)], presets=create_settler, max_cases=300),
    Spec("CGameData_ClearSettlerJob", 0x00510440, "thiscall", [below(LAST_SETTLER + 1)]),
    Spec("CGameData_RestoreSettlerAnimation", 0x0050e380, "thiscall", [below(LAST_SETTLER + 1)]),
]


def job_value(rng, pool):
    r = rng.random()
    if r < 0.4:
        return rng.choice([1, 0x37, 2, 0x35, 0, 0x36, 0x38])
    if r < 0.6:
        return (rng.getrandbits(32) & ~0xFF) | rng.choice([1, 0x37])
    return anything(rng, pool)

SPECS += [
    Spec("CGameData_IsSettlerJob01Or37", 0x004fa6b0, "thiscall", [job_value]),
    Spec("CGameData_JobAfter01Or37", 0x004faa40, "thiscall", [job_value]),
]


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
]


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


def miner_state(rng, pool, args):
    settler = SETTLERS + args[0] * SETTLER_BYTES
    building = rng.randrange(0x1000)
    art = rng.choice([7, 8, 9, 0x1d, 0x25, 0x38, 6, 0x0a, 0x39, rng.getrandbits(8)])
    return [(settler + 0x22, building, 2), (settler + 0x4, rng.randrange(384), 2),
            (settler + 0x6, rng.randrange(384), 2), (BUILDINGS + building * BUILDING_BYTES + 0x6, art, 1)]

SPECS += [
    Spec("CGameData_MinerJobEnter", 0x004f9a00, "thiscall", [below(0x7d00)], presets=miner_state, max_cases=1000),
]


# Package B, batch 3 (settler_idle.cpp, pile_goods_moves.cpp, miner_jobs.cpp)
IDLE_JOBS = [0x37, 0x35, 0xb0, 0x8e, 0xd4, 0xd5, 0x12e, 0x12f, 1, 2, 0x68, 0x69, 3, 4, 0x8a, 0x1c, 0x2a, 0xe5,
             0x168, 5, 6, 7, 8, 9, 0xe, 0xf, 0x11, 0x12, 0x133, 0x134, 0xa, 0xb, 0xa9, 0x4c, 0x4d, 0x4e, 0x4f,
             0xaa, 0xc, 0xd, 0x50, 0xac, 0x100]
IDLE_TYPES = [0, 1, 2, 0x16, 0x28, 0x0f, 0x20, 0x08, 0x1e, 0x05, 0x0a, 0x30]


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

SPECS += [
    Spec("CGameData_ResetSettlerToIdle", 0x0050cf50, "thiscall", [between(1, 0x3000)],
         presets=idle_state, max_cases=1500),
]


# Package B, batch 4 (miner_jobs.cpp: MinerJobFood, MinerJobDig)
MINES = [7, 8, 9, 0x25, 0x1d, 0x38]


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
# max_instructions: one of them (case 409) removes a settler whose lazily
# random owner byte is >= 20, so RemoveSettlerFromLists (0x50dff0) shifts a
# "player list" of random length; in 20M instructions it wrote 8M bytes and
# the worker peaked at 2.6 GB. Every valid case needs fewer than 200,000
# instructions (Release and Debug give the same 1490 cases / 10 invalid /
# full coverage at 200,000 and 1,000,000 as at 20M); 1M leaves a margin
# and bounds the worker at ~250 MB. (Presetting those list lengths isn't an
# option: players 20.. alias the player records.)
SOLDIER_PROBES = {0x0051b640: (2, "thiscall", "probe"), 0x00519a90: (2, "thiscall", "probe")}
SOLDIER_TYPES_B = [5, 0x20, 0x23, 8, 0x1e, 0x21, 0x0f, 0x1f, 0x22]
TARGET_TYPES_B = [5, 0x20, 8, 0x1e, 0x0f, 0x22, 0x16, 0x17, 0x25, 0x27, 0x28, 0x29, 0x2a, 1]
TARGET_JOBS_B = [0x37, 0x8e, 0x35, 0xb0, 3, 4, 0xad, 0xae, 0xad, 0xae, 0x130, 0xd4, 0x12f, 0x135, 0x10, 0x55, 0x151]
MAP_EFFECTS = 0x14d2bc8


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
         max_cases=1500, exempt_blocks=(0x50fde8,), max_instructions=1_000_000,
         # Full coverage takes 1072 cases (52 s alone in Release, 2026-11-24;
         # over the 60 s budget when other workers share the machine):
         # quick runs do 600 cases, --full runs (1500) cover all 294 blocks.
         quick_cases=600),
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
         exempt_blocks=US_EXEMPT,
         # Full coverage takes 22784 cases (1346 s alone in Release,
         # 2026-11-24): quick runs do 600 cases (about 36 s), --full runs
         # (24000) cover all 2236 blocks; the in-process tests and the
         # whole-game replays are its main evidence.
         quick_cases=600),
]
