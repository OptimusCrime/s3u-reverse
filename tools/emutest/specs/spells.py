"""Emulator specs, part `spells`: specs of the replacements in src/s3re/game/spells/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def mana(rng, pool, args):
    record = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    def value():
        return rng.choice([rng.getrandbits(32), rng.randrange(0x200), rng.getrandbits(32) | 0x80000000])
    fields = [(record + 0x4, rng.randrange(5), 4), (record + 0x8c, value(), 4)]
    for slot in range(9):
        fields.append((record + 0x94 + slot * 4, value(), 4))
    return fields

SPECS += [
    # god events, piles, presence (godly_events.cpp, area_piles.cpp, player_presence.cpp)
    Spec("CGameData_HasManaForSpell", 0x0054b240, "thiscall", [below(20), below(36, 0.1)],
         presets=mana),
]


def spell_number(rng, pool):
    return rng.randrange(40)

SPECS += [
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
