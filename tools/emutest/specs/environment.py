"""Emulator specs, part `environment`: specs of the replacements in src/s3re/game/environment/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""

SPECS += [
    Spec("MapEffect_DefaultConstruct", 0x00543830, "thiscall", []),
    Spec("MapEffect_Construct", 0x00543880, "thiscall", [anything] * 20),
]


def keep_or_value(rng, pool) -> int:
    return 0xFFFFFFFF if rng.random() < 0.5 else rng.getrandbits(32)

SPECS += [
    Spec("MapEffect_SetLinks", 0x00544290, "thiscall", [anything, keep_or_value, keep_or_value]),
]

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
    Spec("MapEffect_PositionAt", 0x00544210, "fastcall",
         [one_of(WORLD_BASE + EFFECT_RECORD), anything, effect_tick,
          one_of(WORLD_BASE + EFFECT_OUT, WORLD_BASE + EFFECT_RECORD + 0x8),
          one_of(WORLD_BASE + EFFECT_OUT + 4, WORLD_BASE + EFFECT_RECORD + 0x28)],
         eax_mask=0, presets=effect_record),
    Spec("MapEffects_SetMapSize", 0x00544320, "thiscall",
         [between(-10, 1100), one_of(0xc8, 0, 1, 0xFFFFFFFF, 0x7FFFFFFF)]),
]


def effect_source(rng, pool, args):
    fields = effect_table(0.1)(rng, pool, args)
    used = fields[0][1]
    fields += [(SCRATCH + 0x10, rng.randrange(7000), 4), (SCRATCH + 0x14, rng.randrange(7000), 4),
               (SCRATCH + 0x48, rng.randrange(min(used + 12, 4999) + 1), 4)]
    return fields

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


def decay_traffic_tile(rng, pool, args):
    t = tile(args[0], args[1])
    return [(t + 0x12, rng.choice([0, 7, 8, 9, rng.randrange(256)]), 1),
            (t + 0x6, 0x1c if rng.random() < 0.5 else rng.randrange(256), 1), (t + 0xa, rng.getrandbits(8), 1)]

SPECS += [
    Spec("MapEffect_IsExpired", 0x005440c0, "thiscall", [between(900, 1100)], eax_mask=0xff,
         presets=effect_timing),
    Spec("MapEffects_ExpireSome", 0x00545d40, "thiscall", [between(1000, 1100)], eax_mask=0,
         presets=expiring_effects, max_cases=300),
    Spec("CGameData_DecayTraffic", 0x00503980, "thiscall", [below(64), below(64)], eax_mask=0,
         presets=decay_traffic_tile),
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
