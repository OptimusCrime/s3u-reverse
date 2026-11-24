"""Emulator specs, part `economy`: specs of the replacements in src/s3re/game/economy/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def building_pile_slots(rng, pool, args):
    record = BUILDINGS + args[0] * BUILDING_BYTES
    fields = []
    for slot in range(3):
        pile = 0 if rng.random() < 0.3 else rng.randrange(1, 0x10000)
        fields.append((record + 0x10 + slot * 2, pile, 2))
        fields.append((PILES + pile * PILE_BYTES + 1,
                       0 if rng.random() < 0.4 else rng.randrange(256), 1))
    return fields


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

SPECS += [
    # goods piles (goods_piles.cpp)
    Spec("CGameData_BuildingPileHasGoods", 0x004df9b0, "thiscall",
         [between(0, 2000), below(5, 0.15)], eax_mask=0xff, presets=building_pile_slots),
    Spec("CGameData_RecountPlayerGoods", 0x004ffa50, "thiscall", [], presets=goods_piles,
         max_cases=600),
    Spec("CGameData_PileNeighbourBuildingByteE", 0x00505fd0, "thiscall", [between(1, 3000)],
         presets=pile_neighbourhood),
    Spec("CGameData_AddTilePileToArea", 0x005051b0, "thiscall", [below(384), below(384)],
         presets=tile_pile),
    Spec("CGameData_AddGoodsToPile", 0x00505650, "thiscall", [between(1, 0xfff)], presets=pile_add),
    # batch 17
    Spec("CGameData_PileIncAmountLeaving", 0x005058c0, "thiscall", [between(1, 0xfff)], presets=pile_leaving,
         max_cases=1000),
]


def pile_on_map(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    return [(record + 0x6, rng.randrange(384), 2), (record + 0x8, rng.randrange(384), 2),
            (record + 0x3, rng.choice([0, 0xff, rng.randrange(256)]), 1)]

SPECS += [
    Spec("CGameData_PileIncAmountComing", 0x00505bc0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("CGameData_PileDecAmountComing", 0x00505bf0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("CGameData_RemovePileFromTile", 0x00504650, "thiscall", [below(0x10000)], presets=pile_on_map),
]


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

SPECS += [
    Spec("CGameData_PileDecAmountLeaving", 0x00505ac0, "thiscall", [between(1, 0xfff)], presets=linked_pile),
    Spec("CGameData_MakePileEmpty", 0x00506080, "thiscall", [between(1, 0xfff)], presets=linked_pile),
]


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

SPECS += [
    Spec("CGameData_CreatePile", 0x00504420, "thiscall",
         [below(384), below(384), pile_goods, pile_amount, pile_kind], presets=create_pile_state,
         max_cases=800),
]


def pile_claims(rng, pool, args):
    settler, pile = args
    fields = []
    for slot in range(128):
        fields += [(PILE_CLAIMS + slot * 4, pile if rng.random() < 0.5 else rng.randrange(6), 2),
                   (PILE_CLAIMS + slot * 4 + 2, settler if rng.random() < 0.5 else rng.randrange(6), 2)]
    return fields

SPECS += [
    Spec("CGameData_CountOtherPileClaims", 0x00510800, "thiscall", [below(6, 0.1), below(6, 0.1)],
         presets=pile_claims, max_cases=500),
]


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

SPECS += [
    Spec("CGameData_SetPileGood", 0x005061b0, "thiscall",
         [between(1, 0xfff), lambda rng, pool: rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(36)],
         presets=set_pile_good_state, max_cases=2000),
    Spec("CGameData_DropGoodNear", 0x005046b0, "thiscall",
         [below(36), drop_amount, between(0, 383), between(0, 383)], eax_mask=0,
         presets=drop_good_state, max_cases=300),
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

SPECS += [
    Spec("CGameData_RecountPlayerPopulation", 0x005104c0, "thiscall", [], eax_mask=0, presets=population_world,
         max_cases=400),
    Spec("CGameData_SetTowerSoldierAnimation", 0x0050e200, "thiscall", [between(1, 0x7cfe)], presets=tower_soldier_world,
         globals=lambda rng, pool, args: [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4)], max_cases=1000),
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


def take_good_state(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    amount = rng.choice([1, 1, 2, 0, rng.randrange(256)])
    return pile_on_tile(rng, args[0]) + spiral_offsets(19) + [
        (record + 0x1, amount, 1), (record + 0x2, rng.randrange(4), 1),
        (record + 0x3, 0 if rng.random() < 0.6 else rng.randrange(1, 4), 1),
        (record + 0x4, rng.choice([0, 0, 0xfe, 0xfe, 0xff, 7]), 1)]

SPECS += [
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
        path = S3_EXE_PATH
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
