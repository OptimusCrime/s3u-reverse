"""Emulator specs, part `pathing`: specs of the replacements in src/s3re/game/pathing/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


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

SPECS += [
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
    # About 0.26 s per case (Release); full coverage takes 368 cases (96 s,
    # 2026-11-24), more than the quick budget: quick runs do 120 cases, the
    # full coverage (116/116 blocks) is shown by --full runs (600 cases).
    Spec("Pathing_FollowRightWall", 0x004a4050, "thiscall", [walk_settler, scratch(0)], presets=route_wall,
         max_cases=600, quick_cases=120,
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

SPECS += [
    # CGameData::InitReciprocals (reciprocals.cpp): no inputs; a few cases.
    Spec("CGameData_InitReciprocals", 0x004a1eb0, "thiscall", [], max_cases=5),
]
