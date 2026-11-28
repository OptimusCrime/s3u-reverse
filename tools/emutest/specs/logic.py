"""Emulator specs, part `logic`: specs of the replacements in src/s3re/game/logic/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


# Unused space at the end of the emulated world (32 MB; the game's world is
# about 22 MB) for objects other than the world: a fake CLogicThread (see
# the "logicthread" convention) and scratch buffers that pointer arguments
# point into (lazily random and compared like the world).
LOGIC_THREAD = 0x1f80000


UI_FIELDS = {"CGameData_IsUiSelectionMode": 0x2d284, "CGameData_IsUiField298": 0x2d298,
             "CGameData_IsUiSelectionParam1": 0x2d288, "CGameData_IsUiSelectionParam2": 0x2d28c}


def field_equals_arg_half_the_time(offset: int) -> Callable:
    def presets(rng, pool, args):
        return [(offset, args[0] if rng.random() < 0.5 else anything(rng, pool), 4)]
    return presets


def mode4_field298(rng, pool, args):
    return [(0x2d284, 4 if rng.random() < 0.6 else anything(rng, pool), 4),
            (0x2d298, 2 if rng.random() < 0.6 else anything(rng, pool), 4)]
GAME_OUTCOME = 0x730f64


def game_outcome(rng, pool, args):
    return [(GAME_OUTCOME, rng.choice([1, 0, 0xFFFFFFFF, 2, rng.getrandbits(32)]), 4)]


def ui_feedback_count(rng, pool, args):
    return [(0x15394ac, rng.randrange(11) if rng.random() < 0.8 else 10, 4)]


def camera(rng, pool, args):
    def value():
        return rng.choice([rng.randint(-5, 5), rng.randint(0, 10000), rng.getrandbits(32)])
    return [(0x2d25c, value() & 0xFFFFFFFF, 4), (0x2d260, value() & 0xFFFFFFFF, 4),
            (MAP_SIZE, rng.choice([0, 64, 384, rng.getrandbits(32)]), 4)]


def camera_tile(rng, pool, args):
    return [(MAP_SIZE, rng.choice([64, 384, rng.randrange(400), rng.getrandbits(32)]), 4)]


def game_end_buildings(rng, pool, args):
    count = rng.choice([0, 1, rng.randrange(40)])
    fields = [(0x1499e60, count, 4)]
    for player in range(255):
        fields.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES,
                       0 if rng.random() < 0.25 else rng.getrandbits(32) | 1, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        player = 0xff if rng.random() < 0.15 else rng.randrange(255) if rng.random() < 0.05 \
            else rng.randrange(20)
        fields += [(record + 0x0, player, 1),
                   (record + 0x21, 0 if rng.random() < 0.7 else rng.randrange(256), 1),
                   (record + 0x6, rng.randrange(0xe, 0x14), 1)]
    return fields


def area_piles(rng, pool, args):
    count = rng.choice([0, 1, rng.randrange(30)])
    fields = [(0xe115dc, count, 4)]
    next_pile = 1
    for area in range(1, count + 1):
        record = AREAS + area * AREA_BYTES
        player = 0xff if rng.random() < 0.15 else rng.randrange(255) if rng.random() < 0.05 \
            else rng.randrange(20)
        first = 0
        for _ in range(rng.randrange(5)):
            fields += [(PILES + next_pile * PILE_BYTES + 0xc, first, 2),
                       (PILES + next_pile * PILE_BYTES + 0x1, rng.getrandbits(8), 1)]
            first = next_pile
            next_pile += 1
        fields += [(record + 0x0, player, 1), (record + 0x1e, first, 2)]
    return fields


def local_player_camera(rng, pool, args):
    player = rng.randrange(20)
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    return [(0x2d2d4, player, 4), (record + 0x10, rng.randrange(384), 4), (record + 0x14, rng.randrange(384), 4),
            (MAP_SIZE, rng.choice([64, 384, rng.randrange(400)]), 4)]


def line_args() -> list[Generator]:
    """(x0, y0, x1, y1, value): end points on the map, often close; rarely
    a far end point whose tile address wraps around to (x0, y0) (deltas of
    0x50000000 and -0x60000000: distance 2^31 or more)."""
    state = {}

    def x0(rng, pool):
        state["far"] = rng.random() < 0.05
        state["x0"] = rng.randrange(384)
        return state["x0"]

    def y0(rng, pool):
        state["y0"] = rng.randrange(384)
        return state["y0"]

    def x1(rng, pool):
        if state["far"]:
            return (state["x0"] + 0x50000000) & 0xFFFFFFFF
        if rng.random() < 0.3:
            return min(383, max(0, state["x0"] + rng.randint(-12, 12)))
        return rng.randrange(384)

    def y1(rng, pool):
        if state["far"]:
            return (state["y0"] - 0x60000000) & 0xFFFFFFFF
        if rng.random() < 0.3:
            return min(383, max(0, state["y0"] + rng.randint(-12, 12)))
        return rng.randrange(384)

    return [x0, y0, x1, y1, anything]


def line_map(rng, pool, args):
    return [(MAP_SIZE, rng.choice([384, rng.randrange(1, 384), rng.getrandbits(32)]), 4)]


WORK_AREA_TYPES = {0x02: 3000, 0x03: 3000, 0x05: 1500, 0x0e: 150, 0x18: 300, 0x19: 2000, 0x1e: 800,
                   0x24: 400, 0x33: 1500}


def work_area(rng, pool, args):
    """The building's type (mostly one with a small working area), buildup,
    centre and tile; the offset table up to the type's entry with
    nondecreasing values (sometimes all equal: nothing drawn) and small
    offsets; areas of the tiles near the centre 0 or 1."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    r = rng.random()
    if r < 0.6:
        btype = rng.choice([0x0e, 0x18, 0x24, 0x1e])
    elif r < 0.85:
        btype = rng.choice(list(WORK_AREA_TYPES))
    else:
        btype = rng.choice([0, 1, 0x30, 0xff, rng.randrange(256)])
    n = rng.choice([384, rng.randrange(20, 384)])
    cx, cy = rng.randrange(n), rng.randrange(n)
    bx, by = min(n - 1, max(0, cx + rng.randint(-3, 3))), min(n - 1, max(0, cy + rng.randint(-3, 3)))
    fields = [(MAP_SIZE, n, 4), (record + 0x6, btype, 1),
              (record + 0x21, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1),
              (record + 0xa, bx, 2), (record + 0xc, by, 2), (tile_area_offset(bx, by), 1, 2),
              (SEARCH_GENERATION, rng.choice([0xff, rng.randrange(255)]), 1)]
    for field in ((0x30, 0x32), (0xe, 0x14)):
        fields += [(record + field[0], cx, 2), (record + field[1], cy, 2)]
    entries = WORK_AREA_TYPES.get(btype, 0)
    flat = rng.random() < 0.05
    value = rng.randrange(3)
    reach = 1
    for i in range(entries + 1):
        if not flat and rng.random() < 0.4:
            value += rng.randrange(1, 3)
        if i % 60 == 59:
            reach += 1
        fields += [(NEIGHBOUR_OFFSETS + i * 16, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + i * 16 + 4, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + i * 16 + 8, value, 4)]
    if flat or rng.random() < 0.05:
        fields.append((NEIGHBOUR_OFFSETS + 8, value + 5, 4))  # entry 0 above r: nothing drawn
    for x in range(max(cx - reach - 1, 0), min(cx + reach + 2, n)):
        for y in range(max(cy - reach - 1, 0), min(cy + reach + 2, n)):
            fields.append((tile_area_offset(x, y), 1 if rng.random() < 0.7 else 0, 2))
    return fields

SPECS += [
    # UI state queries (ui_state.cpp)
    *[Spec(name, address, "thiscall", [anything],
           presets=field_equals_arg_half_the_time(UI_FIELDS[name]))
      for name, address in [("CGameData_IsUiSelectionMode", 0x005157b0),
                            ("CGameData_IsUiField298", 0x005157d0),
                            ("CGameData_IsUiSelectionParam1", 0x005157f0),
                            ("CGameData_IsUiSelectionParam2", 0x00515810)]],
    Spec("CGameData_IsUiSelectionMode4Field298Is2", 0x00515830, "thiscall", [],
         presets=mode4_field298),
    Spec("CGameData_IsGameOutcome1", 0x00515ad0, "thiscall", [anything], globals=game_outcome),
    Spec("CGameData_ScreenCenterTileX", 0x005158f0, "thiscall", [], globals=screen_size),
    Spec("CGameData_ScreenCenterTileY", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("CGameData_ClampCamera", 0x0051b4d0, "thiscall", [], presets=camera,
         globals=screen_size),
    Spec("CGameData_PushUiFeedback", 0x004febe0, "thiscall", [anything],
         presets=ui_feedback_count),
    # camera, random, logic thread (ui_state.cpp, random.cpp)
    Spec("CGameData_CenterCameraOnTile", 0x0051b560, "thiscall", [below(384), below(384)],
         presets=camera_tile, globals=screen_size),
    Spec("CLogicThread_PushUiFeedback", 0x0053f560, "logicthread", [anything],
         presets=ui_feedback_count),
    # game-end counts (game_end.cpp)
    Spec("CLogicThread_CountMilitaryBuildings", 0x00514a00, "logicthread", [scratch(0)],
         presets=game_end_buildings, max_cases=500),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514a90, "logicthread",
         [below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_CountTilesOwnedByEither", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_SumGoldPerPlayer", 0x00514b40, "logicthread", [scratch(0)],
         presets=area_piles),
    Spec("CGameData_CenterCameraOnLocalPlayer", 0x0051b6b0, "thiscall", [],
         presets=local_player_camera, globals=screen_size),
    Spec("CLogicThread_DrawOverlayLine", 0x00523560, "logicthread", line_args(), presets=line_map,
         max_cases=500),
    Spec("CLogicThread_DrawWorkAreaOverlay", 0x00523840, "logicthread", [between(1, 0x100)],
         presets=work_area, max_cases=150),
]


# ---------------------------------------------------------------------------
# batch 18
# ---------------------------------------------------------------------------

BUILDING_EXTRAS = 0x10f45e8
BUILDING_EXTRA_BYTES = 0x5a


def building_route(rng, pool, args):
    """The building's type (mostly 0x21/0x22), its start words, its extra
    record with waypoints (sometimes unset) and end point (rarely unset)."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    number = rng.randrange(1, 999)
    extra = BUILDING_EXTRAS + number * BUILDING_EXTRA_BYTES
    n = rng.choice([384, rng.randrange(1, 384)])
    fields = [(MAP_SIZE, n, 4), (record + 0x6, rng.choice([0x21, 0x22, 0x21, 0x22, 0, 0x20, 0xff]), 1),
              (record + 0x34, number, 2), (SEARCH_GENERATION, 0xff if rng.random() < 0.05 else rng.randrange(255), 1)]
    for field in (0x2, 0x4, 0x30, 0x32):
        fields.append((record + field, rng.randrange(384), 2))
    for point in range(4):
        unset = rng.random() < (0.1 if point == 3 else 0.3)
        fields += [(extra + 0x2 + point * 2, 0xffff if unset else rng.randrange(384), 2),
                   (extra + 0xa + point * 2, rng.randrange(384), 2)]
    return fields


# --- game-end check (game_end.cpp, victory_conditions.cpp, objectives.cpp,
# game_end_check.cpp), batch 46 -----------------------------------------------

GAME_TICK = 0x7dfd48
GAME_END_MISSION = 0x7dfdb0  # g_campaignMission
WORLD_UPDATE_DISABLED = 0x7deb55
PLAYER_TALLIES = 0x7acfd4
PLAYER_COLOUR_INDEX = 0x7ace64
VICTORY_CONDITIONS = 0x15394b1
OFFSET_ENTRIES = 48
GAME_END_TICKS = (56250, 65625, 70312, 112500)
GAME_END_HARD_MISSIONS = (27, 30, 33, 34, 35, 36, 37, 39, 40, 41, 42, 43, 45, 47, 48, 51, 52, 54, 56, 58, 66, 68,
                          69, 72)

# Places the campaign missions look at (CheckGameEnd); the tiles around them
# get buildings, piles and settlers from the small test tables.
GAME_END_CENTRES = (
    (122, 112), (295, 194), (314, 208), (300, 222), (319, 249), (412, 504), (171, 326), (118, 541), (207, 358),
    (370, 335), (411, 266), (332, 74), (357, 292), (540, 47), (251, 74), (568, 387), (340, 576), (67, 319),
    (603, 34), (56, 607), (93, 43), (40, 250), (103, 381), (468, 506), (222, 342), (157, 394), (508, 463),
    (256, 420), (234, 425), (224, 438), (258, 444), (250, 456), (243, 540), (260, 551), (180, 483), (235, 497),
    (238, 542), (178, 434), (476, 37), (344, 430), (267, 536), (274, 522), (293, 508), (310, 510), (324, 511),
    (344, 519), (359, 527), (376, 533), (404, 554), (373, 548), (365, 588), (347, 595), (330, 599), (304, 592),
    (296, 576), (331, 571), (433, 538), (196, 67), (202, 53), (252, 54), (278, 70), (480, 474), (501, 494),
    (525, 480), (509, 466), (129, 90), (138, 95), (198, 138), (283, 193), (345, 209), (371, 230), (417, 218),
    (276, 149), (230, 100), (250, 70), (280, 100), (430, 320), (310, 220), (300, 180), (360, 180), (360, 140),
    (350, 100), (400, 100), (270, 20), (380, 20), (115, 175), (65, 150), (95, 120), (260, 290), (320, 290),
    (380, 290), (440, 290), (280, 340), (160, 250), (220, 250), (230, 160), (290, 160), (50, 50), (110, 50),
)
GAME_END_ARTS = (8, 15, 16, 18, 22, 25, 28, 34, 36, 41, 42, 43, 44, 45, 50)
GAME_END_SETTLER_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35, 0, 1, 36)


def small_owner(rng):
    return 0xff if rng.random() < 0.12 else rng.randrange(5)


def offset_rings(rng, entries=OFFSET_ENTRIES, wide=False):
    """The offset table: entries of growing (signed) distance, then -1 (the
    end for any radius below 0xffffffff)."""
    fields = []
    distance = rng.randint(-1, 30) if rng.random() < 0.1 else 10 if wide and rng.random() < 0.1 else 0
    for k in range(entries + 1):
        reach = 5 if wide else 3
        dx, dy = rng.randint(-reach, reach), rng.randint(-reach, reach)
        if rng.random() < 0.35:
            distance += 1
        stored = 0xFFFFFFFF if k == entries else distance & 0xFFFFFFFF
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, stored, 4)]
    return fields


def game_end_tile(rng, x, y, buildings=40, settlers=64, local=0):
    flags = rng.getrandbits(8) & ~0x21
    if rng.random() < 0.5:
        flags |= 0x20
    if rng.random() < 0.25:
        flags |= 0x01
    owner = local if rng.random() < 0.3 else small_owner(rng)
    return [(tile(x, y) + 0x8, owner, 1), (tile(x, y) + 0xa, flags, 1),
            (tile(x, y) + 0x2, 0 if rng.random() < 0.15 else rng.randint(1, buildings), 2),
            (tile(x, y) + 0x0, 0 if rng.random() < 0.3 else rng.randint(1, settlers), 2),
            (tile(x, y) + 0x6, 0x80 if rng.random() < 0.5 else rng.randrange(256), 1),
            (tile(x, y) + 0x10, rng.randrange(3), 1)]


def game_end_buildings_table(rng, count):
    fields = [(BUILDING_COUNT, count, 4)]
    for b in range(1, 41):
        record = BUILDINGS + b * BUILDING_BYTES
        art = rng.choice(GAME_END_ARTS) if rng.random() < 0.8 else rng.randrange(60)
        fields += [(record + 0x0, small_owner(rng), 1), (record + 0x6, art, 1),
                   (record + 0x21, 0 if rng.random() < 0.8 else rng.randint(1, 200), 1),
                   (record + 0x34, rng.randrange(64), 2)]
    return fields


def game_end_piles(rng):
    """Piles 1..40 (on tiles): a few goods and amounts."""
    fields = []
    for p in range(1, 41):
        good = rng.choice((8, 11, 8, 11, rng.randrange(36)))
        fields += [(PILES + p * PILE_BYTES, good, 1), (PILES + p * PILE_BYTES + 1, rng.randrange(30), 1)]
    return fields


def game_end_areas(rng, local):
    """Areas 1..10 with pile chains (piles from 100 on) for every good."""
    count = rng.choice([0, 1, rng.randrange(11), 10])
    fields = [(0xe115dc, count, 4)]
    next_pile = 100
    for area in range(1, 11):
        record = AREAS + area * AREA_BYTES
        owner = local if rng.random() < 0.4 else small_owner(rng)
        fields.append((record, owner, 1))
        for good in range(36):
            first = 0
            for _ in range(0 if rng.random() < 0.5 else rng.randrange(5)):
                fields += [(PILES + next_pile * PILE_BYTES + 0xc, first, 2),
                           (PILES + next_pile * PILE_BYTES + 0x1, rng.randrange(70), 1)]
                first = next_pile
                next_pile += 1
            fields.append((record + 0xa + good * 2, first, 2))
    return fields


def game_end_players(rng):
    fields = []
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(record + 0x0, 0 if rng.random() < 0.2 else rng.choice([1, rng.getrandbits(32) | 1]), 4),
                   (record + 0xc, rng.randrange(3), 4),
                   (record + 0x8c, rng.randint(150, 250), 4),
                   (record + 0x80, rng.randint(190, 300), 4),
                   (record + 0x12c, rng.randrange(400), 4),
                   (record + 0x134, 0 if rng.random() < 0.5 else rng.getrandbits(13), 4)]
    return fields


def victory_conditions(rng, local, places=GAME_END_CENTRES):
    """VictoryConditions (world+0x15394b1) with each condition on in a third
    of the cases; places on the map."""
    vc = VICTORY_CONDITIONS
    fields = [(vc + 0x00, int(rng.random() < 0.35), 1)]
    fields += [(vc + 0x01 + i, int(rng.random() < 0.15), 1) for i in range(20)]
    fields.append((vc + 0x15, int(rng.random() < 0.35), 1))
    for i in range(10):
        player = 0xff if rng.random() < 0.4 else rng.choice([rng.randrange(5), rng.randrange(5), 0xfe, 0x80])
        art = rng.choice(GAME_END_ARTS) if rng.random() < 0.8 else rng.choice([0x80, 0xff, rng.randrange(60)])
        fields += [(vc + 0x16 + i * 2, player, 1), (vc + 0x17 + i * 2, art, 1)]
    fields.append((vc + 0x2a, int(rng.random() < 0.35), 1))
    for i in range(5):
        x, y = rng.choice(places)
        fields += [(vc + 0x2b + i * 5, int(rng.random() < 0.5), 1), (vc + 0x2c + i * 5, x, 2),
                   (vc + 0x2e + i * 5, y, 2)]
    fields.append((vc + 0x44, int(rng.random() < 0.35), 1))
    for i in range(20):
        minutes = 0 if rng.random() < 0.8 else rng.choice([rng.randrange(4), rng.randrange(300), 0xffff, 40000])
        fields.append((vc + 0x45 + i * 2, minutes, 2))
    fields.append((vc + 0x6d, int(rng.random() < 0.35), 1))
    for i in range(3):
        fields += [(vc + 0x6e + i * 3, 0 if rng.random() < 0.3 else rng.randrange(60), 2),
                   (vc + 0x70 + i * 3, rng.randrange(36) if rng.random() < 0.9 else rng.choice([0xff, 0x80]), 1)]
    return fields


def game_end_settlers(rng, local):
    count = rng.choice([0, 1, rng.randrange(65), 64])
    fields = [(SETTLER_COUNT, count, 4)]
    for s in range(0, 66):
        owner = local if rng.random() < 0.3 else small_owner(rng)
        fields += [(SETTLERS + s * SETTLER_BYTES, owner, 1),
                   (SETTLERS + s * SETTLER_BYTES + 1, rng.choice(GAME_END_SETTLER_TYPES), 1)]
    return fields


# Buildings the missions look for (CheckGameEnd: IsBuildingNear goals and the
# tower places of missions 42/45/69, the marketplaces of 34/58): (x, y, art,
# owner) with owner "local", "ally" (another player of the local team),
# "any" or a player number.
GAME_END_GOALS = (
    (332, 74, 44, "local"), (357, 292, 18, "local"), (540, 47, 25, "local"), (251, 74, 15, "local"),
    (568, 387, 15, "local"), (340, 576, 15, "local"), (67, 319, 15, "local"), (603, 34, 41, "local"),
    (56, 607, 41, "local"), (468, 506, 45, "local"), (508, 463, 45, "any"), (256, 420, 41, 3), (234, 425, 42, 3),
    (224, 438, 42, 3), (258, 444, 42, 3), (250, 456, 42, 3), (243, 540, 50, "any"), (260, 551, 50, "any"),
    (180, 483, 15, 0), (235, 497, 15, 0), (238, 542, 15, 0), (267, 536, 18, 0), (274, 522, 16, 0),
    (293, 508, 16, 0), (310, 510, 16, 0), (324, 511, 16, 0), (344, 519, 15, 0), (359, 527, 16, 0),
    (376, 533, 16, 0), (404, 554, 16, 0), (373, 548, 16, 0), (365, 588, 16, 0), (347, 595, 16, 0),
    (330, 599, 16, 0), (304, 592, 16, 0), (296, 576, 16, 0), (331, 571, 15, 0), (433, 538, 16, 0),
    (196, 67, 15, 0), (202, 53, 16, 0), (252, 54, 15, 0), (278, 70, 16, 0), (480, 474, 15, 0), (501, 494, 16, 0),
    (525, 480, 15, 0), (509, 466, 16, 0), (129, 90, 15, 0), (138, 95, 16, 0), (412, 504, 34, "ally"),
    (171, 326, 34, "ally"), (118, 541, 43, "any"),
)
GOAL_BUILDINGS = 41  # goal k uses building 41 + k (outside the counted 1..40)


def game_end_world(rng, pool, args):
    local = rng.randrange(4)
    ally = (local + 1) % 4
    # The last check: random (in time), or just before the ticks game_end_globals favours (too early).
    last = rng.getrandbits(32) if rng.random() < 0.8 else rng.choice(GAME_END_TICKS) - rng.randrange(20, 30)
    scenario = rng.choice(("random", "random", "army", "army", "peace", "goals"))
    fields = [(MAP_SIZE, 768 if rng.random() < 0.9 else rng.randrange(300, 768), 4),
              (0x1195dcc, int(rng.random() < 0.15), 1), (0x2d248, rng.randrange(150), 4),
              (LOGIC_THREAD + 0x24c, last & 0xFFFFFFFF, 4)]
    fields += [(0x1195dcd + k, rng.randrange(36), 1) for k in range(7)]
    fields += game_end_players(rng)
    if scenario == "peace":
        local = 0
    fields += [(LOCAL_PLAYER, local, 4)]
    team = rng.randrange(3)
    fields += [(PLAYER_RECORDS + local * PLAYER_RECORD_BYTES + 0xc, team, 4),
               (PLAYER_RECORDS + ally * PLAYER_RECORD_BYTES + 0xc, team, 4)]
    # Buildings 1..40 (counted by CountMilitaryBuildings, up to the count);
    # 1..5 mostly military buildings of players 0..4.
    count = rng.choice([0, rng.randrange(41), 40, 40])
    fields += game_end_buildings_table(rng, count)
    for player in range(5):
        record = BUILDINGS + (player + 1) * BUILDING_BYTES
        military = (rng.random() < 0.75 or (scenario == "peace" and player == 0)) and \
            (scenario not in ("peace", "goals") or player not in (2, 3)) and (scenario != "peace" or player == 0)
        fields += [(record + 0x0, player, 1), (record + 0x21, 0, 1),
                   (record + 0x6, rng.choice((15, 16, 18)) if military else rng.choice((8, 22, 28, 36, 41, 42)), 1)]
    if scenario == "peace":  # nobody but player 0 has military buildings
        for b in range(6, 41):
            fields.append((BUILDINGS + b * BUILDING_BYTES, rng.choice((0, 0, 0xff)), 1))
    elif scenario == "goals":  # the buildings mission 34 wants, of the local player; a gold mine of player 1
        for b, art, owner in zip(range(6, 13), (41, 42, 28, 41, 42, 28, 8), (local,) * 6 + (1,)):
            fields += [(BUILDINGS + b * BUILDING_BYTES, owner, 1), (BUILDINGS + b * BUILDING_BYTES + 0x6, art, 1),
                       (BUILDINGS + b * BUILDING_BYTES + 0x21, 0, 1)]
    fields += game_end_settlers(rng, local)
    army = scenario == "army" or (scenario == "peace" and rng.random() < 0.5)
    if army:  # 130 soldiers of one player (missions 27, 35, 37, 47, 51, 54)
        owner = rng.choice((local, local, 1, 3))
        fields.append((SETTLER_COUNT, 130, 4))
        for s in range(0, 132):
            fields += [(SETTLERS + s * SETTLER_BYTES, owner, 1),
                       (SETTLERS + s * SETTLER_BYTES + 1, rng.choice((33, 34, 35, 33, 34, 35, 5, 8, 15, 30)), 1)]
    fields += game_end_piles(rng)
    fields += game_end_areas(rng, 1 if scenario == "goals" and rng.random() < 0.5 else local)
    fields += offset_rings(rng, wide=True)
    if rng.random() < 0.7:  # the place itself first
        fields += [(NEIGHBOUR_OFFSETS, 0, 4), (NEIGHBOUR_OFFSETS + 4, 0, 4), (NEIGHBOUR_OFFSETS + 8, 0, 4)]
    fields += victory_conditions(rng, local)
    reach = 5 if army else 4
    for cx, cy in GAME_END_CENTRES:
        for dx in range(-reach, reach + 1):
            for dy in range(-reach, reach + 1):
                if (dx or dy) and not army and rng.random() < 0.6:
                    continue
                fields += game_end_tile(rng, cx + dx, cy + dy, settlers=130 if army else 64,
                                        local=local)
                if army:
                    fields.append((tile(cx + dx, cy + dy), rng.randint(1, 130), 2))
    every_goal = scenario == "goals" or rng.random() < 0.3
    for k, (x, y, art, owner) in enumerate(GAME_END_GOALS):
        if not every_goal and rng.random() < 0.5:
            continue
        building = GOAL_BUILDINGS + k
        record = BUILDINGS + building * BUILDING_BYTES
        player = {"local": local, "ally": ally, "any": rng.randrange(5)}.get(owner, owner)
        exact = scenario == "goals"
        if not exact and rng.random() < 0.1:
            player = small_owner(rng)
        fields += [(tile(x, y) + 0x2, building, 2), (tile(x, y) + 0xa, 0x20 | (rng.getrandbits(8) & 0xde), 1),
                   (tile(x, y) + 0x8, player, 1), (record + 0x0, player, 1), (record + 0x6, art, 1),
                   (record + 0x21, 0 if exact or rng.random() < 0.95 else 1, 1), (record + 0x34, rng.randrange(64), 2)]
    if rng.random() < 0.5:  # all places of the "own every place" goals held
        owner = local if rng.random() < 0.5 else 0
        for x, y in GAME_END_CENTRES:
            fields.append((tile(x, y) + 0x8, owner, 1))
    if rng.random() < 0.3:  # mission 33's places held by players 1 and 2
        for x, y in ((295, 194), (314, 208), (300, 222), (319, 249)):
            fields.append((tile(x, y) + 0x8, rng.choice((1, 2)), 1))
    return fields

def game_end_globals(rng, pool, args):
    tick = rng.randrange(130000) if rng.random() < 0.5 else rng.choice(GAME_END_TICKS)
    if rng.random() < 0.3:
        tick += rng.randint(-2, 2)
    # Half the cases go to the missions whose goals need rare worlds.
    mission = rng.randrange(101) if rng.random() < 0.5 else rng.choice(GAME_END_HARD_MISSIONS)
    if rng.random() < 0.03:
        mission = rng.getrandbits(32)
    outcome = 0xFFFFFFFF if rng.random() < 0.95 else rng.choice([0, 1, 2])
    fields = [(GAME_TICK, tick, 4), (GAME_END_MISSION, mission, 4), (GAME_OUTCOME, outcome, 4),
              (WORLD_UPDATE_DISABLED, 0, 1)]
    for p in range(20):
        fields.append((PLAYER_TALLIES + p * 0x44 + 0xc, rng.choice([rng.randint(450, 650), rng.getrandbits(32)]), 4))
        fields.append((PLAYER_COLOUR_INDEX + p * 2, rng.randrange(8), 2))
    return fields


def victory_world(rng, pool, args):
    local = rng.randrange(4)
    fields = [(MAP_SIZE, 768, 4), (LOCAL_PLAYER, local, 4)]
    fields += game_end_players(rng)
    fields += game_end_buildings_table(rng, rng.choice([0, rng.randrange(41), 40]))
    fields += game_end_areas(rng, local)
    fields += victory_conditions(rng, local, places=((10, 10), (11, 12), (20, 5), (7, 30)))
    for x, y in ((10, 10), (11, 12), (20, 5), (7, 30)):
        fields.append((tile(x, y) + 0x8, local if rng.random() < 0.6 else small_owner(rng), 1))
    return fields


def victory_globals(rng, pool, args):
    tick = rng.randrange(40 * 938) if rng.random() < 0.7 else rng.getrandbits(32)
    fields = [(GAME_TICK, tick, 4), (GAME_END_MISSION, 0 if rng.random() < 0.5 else rng.randint(1, 72), 4)]
    fields += [(PLAYER_COLOUR_INDEX + p * 2, rng.randrange(8), 2) for p in range(20)]
    return fields


def ngo_world(rng, pool, args):
    fields = [(LOCAL_PLAYER, rng.randrange(20), 4)]
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(record, 0 if rng.random() < 0.3 else rng.getrandbits(32) | 1, 4), (record + 0xc, rng.randrange(3), 4)]
        for good in range(36):
            fields.append((record + 0xdc + good * 2, rng.randrange(4) if rng.random() < 0.8 else rng.getrandbits(16), 2))
    fields += [(0x1195dcd + k, rng.randrange(36) if rng.random() < 0.9 else rng.randrange(256), 1) for k in range(7)]
    return fields


def colour_players(rng, pool, args):
    return [(PLAYER_RECORDS + p * PLAYER_RECORD_BYTES, 0 if rng.random() < 0.3 else 1, 4) for p in range(20)]


def player_colour_globals(rng, pool, args):
    return [(PLAYER_COLOUR_INDEX + p * 2, rng.randrange(24), 2) for p in range(20)]


def neighbourhood(rng, pool, args):
    """IsBuildingNear / SumGoodsNear: offsets, and tiles around (args[0],
    args[1]) with buildings 1..40 / piles 1..40."""
    x, y = args[0], args[1]
    fields = [(MAP_SIZE, 400 if rng.random() < 0.85 else rng.randrange(401), 4)]
    fields += offset_rings(rng, entries=36)
    fields += game_end_buildings_table(rng, 40)[1:]
    fields += game_end_piles(rng)
    for dx in range(-3, 4):
        for dy in range(-3, 4):
            if 0 <= x + dx < 400 and 0 <= y + dy < 400:
                fields += game_end_tile(rng, x + dx, y + dy)
    return fields


def game_end_player_arg(rng, pool):
    return rng.choice([0xFFFFFFFF, 0xFFFFFFFE, 0x80000000, rng.randrange(5), rng.randrange(5)])

SPECS += [
    Spec("CLogicThread_IsBuildingNear", 0x00514bf0, "logicthread",
         [between(3, 396), between(3, 396), lambda rng, pool: rng.choice([0, 1, 2, 3, 5, 0xFFFFFFFE, 0x80000000]),
          lambda rng, pool: rng.choice(GAME_END_ARTS + (rng.getrandbits(32),)), game_end_player_arg],
         eax_mask=0xFF, presets=neighbourhood, max_cases=800),
    Spec("CLogicThread_SumGoodsNear", 0x00514cc0, "logicthread",
         [between(3, 396), between(3, 396), lambda rng, pool: rng.choice([0, 1, 2, 3, 5, 0xFFFFFFFE, 0x80000000]),
          lambda rng, pool: rng.choice((8, 11, rng.getrandbits(32))), game_end_player_arg],
         presets=neighbourhood, max_cases=800),
    Spec("CGameData_DecideNgoOutcome", 0x004ffc80, "thiscall", [], presets=ngo_world, max_cases=800),
    Spec("CGameData_PlayerOfColour", 0x004ffe00, "thiscall",
         [lambda rng, pool: rng.choice([rng.randrange(24), rng.getrandbits(32)])],
         presets=colour_players, globals=player_colour_globals),
    Spec("CLogicThread_CheckVictoryConditions", 0x005145c0, "logicthread", [], presets=victory_world,
         globals=victory_globals, max_cases=1000),
    # The jump table of the 100 missions follows the code at 0x514424 (data, not code).
    # Full coverage takes 2752 cases (188 s alone in Release, 2026-11-24):
    # quick runs do 500 cases (about 35 s, room for a busy machine), --full
    # runs (10000) cover all 512 blocks.
    Spec("CLogicThread_CheckGameEnd", 0x00511c50, "logicthread", [], eax_mask=0xFF, presets=game_end_world,
         globals=game_end_globals, cover_until=0x00514424, case_scale=5, quick_cases=500),
]

SPECS += [
    Spec("CLogicThread_DrawBuildingRouteOverlay", 0x005236b0, "logicthread", [between(1, 0x100)], eax_mask=0,
         presets=building_route, max_cases=400),
]


# --- batch 20 ---------------------------------------------------------------

NGO_OBJECTIVES = 0x1195dcd


def ngo_id(rng, pool):
    """Objective ids 0..39, sometimes with high bits (only the low byte counts)."""
    return rng.randrange(40) | (rng.getrandbits(32) & ~0xFF if rng.random() < 0.25 else 0)


def ngo_objectives(rng, pool, args):
    return [(NGO_OBJECTIVES + k, rng.randrange(40), 1) for k in range(8)]

SPECS += [
    Spec("CGameData_IsNgoObjectiveChosen", 0x004ffb90, "thiscall", [ngo_id], eax_mask=0xFF, presets=ngo_objectives),
]


GROUP_COUNTS = 0x007ad620
GROUP_SETTLERS = 0x007ad634


def group_menu_state(rng, pool, args):
    group = args[0]
    count = rng.randrange(2) if rng.random() < 0.3 else rng.randrange(101)
    kind = rng.choice([0x24, 0x2c, 0x2b, 0x29, 0x28, 0x16, 0x17, 0x01])
    same = rng.random() < 0.7
    local = rng.randrange(4)
    world = [(0x2d2d4, local, 4), (0x2d298, rng.randrange(8) if rng.random() < 0.7 else rng.getrandbits(32), 4)]
    exe = [(GROUP_COUNTS + group * 2, count, 2)]
    for k in range(count):
        settler = rng.randrange(0x10000)
        exe.append((GROUP_SETTLERS + group * 200 + k * 2, settler, 2))
        world.append((SETTLERS + settler * 0x40 + 1,
                      kind if same or rng.random() < 0.5 else rng.choice([0x24, 0x2c, 0x17, 0x02]), 1))
        world.append((SETTLERS + settler * 0x40, local if rng.random() < 0.6 else rng.randrange(4), 1))
    return world, exe


def group_menu_presets(rng, pool, args):
    """Draws the state once per case (world part); the S3.EXE part is
    replayed by group_menu_globals from the same draw."""
    world, exe = group_menu_state(rng, pool, args)
    GROUP_MENU_EXE[:] = exe
    return world


GROUP_MENU_EXE: list = []


def group_menu_globals(rng, pool, args):
    return list(GROUP_MENU_EXE)

SPECS += [
    Spec("CLogicThread_UpdateGroupMenu", 0x00542990, "logicthread", [below(10)],
         presets=group_menu_presets, globals=group_menu_globals),
]


def clear_around_world(rng, pool, args):
    x, y = args
    centre = (x * 0x300 + y) * 24 + TILES
    flags = rng.getrandbits(8)
    fields = [(centre + 0xa, flags if rng.random() < 0.25 else (flags | 0x10) & ~1, 1),
              (centre + 6, 0x20 + rng.randrange(16) if rng.random() < 0.05 else rng.randrange(0x20), 1),
              (0xe0788c, 9999 if rng.random() < 0.1 else rng.randrange(10000), 4)]
    for delta in RADIUS2:
        rare = rng.random() < 0.04
        f = rng.getrandbits(8)
        fields.append((centre + delta * 24 + 0xa, f | 1 if rare else f & ~1, 1))
        rare = rng.random() < 0.04
        fields.append((centre + delta * 24 + 6, 0x20 + rng.randrange(16) if rare else rng.randrange(0x20), 1))
    return fields


def settler_animation_world(rng, pool, args):
    settler, kind = args
    race = rng.randrange(4)
    fields = [(0x12a5e60 + settler * 0x40 + 2, race, 1)]
    animation = animation_table()[kind * 4 + race]
    group, index = animation >> 16, animation & 0xFFFF
    if group < 0x130:
        fields += [(GFX_OBJECT + 0xb24 + group * 4, WORLD_BASE + GFX_POINTERS, 4),
                   (GFX_OBJECT + 0x264 + group * 4, 0, 4),
                   (GFX_POINTERS + 8 + index * 4, WORLD_BASE + GFX_HEADERS + rng.randrange(0x100), 4)]
    return fields


def remove_from_groups_globals(rng, pool, args):
    fields = []
    for group in range(10):
        count = 0 if rng.random() < 0.25 else rng.randrange(101)
        fields.append((GROUP_COUNTS + group * 2, count, 2))
        for k in range(100):
            fields.append((GROUP_SETTLERS + group * 200 + k * 2, rng.randrange(6), 2))
    return fields

SPECS += [
    Spec("CGameData_WearPath", 0x0051b7f0, "thiscall", [TILE, TILE], presets=wear_path_world),
    Spec("CGameData_QueueMarkedTileWithRoom", 0x0051d000, "thiscall", [TILE, TILE],
         presets=clear_around_world),
    Spec("CGameData_SetSettlerAnimation", 0x0050e2c0, "thiscall", [below(0x8000), below(40)],
         presets=settler_animation_world,
         globals=lambda rng, pool, args: [(0x007a8010, WORLD_BASE + GFX_OBJECT, 4)]),
    Spec("CLogicThread_RemoveSettlerFromGroups", 0x005428e0, "fastcall",
         [anything, anything, id_with_noise(6), one_of(0, 1, 2, 5, 9, 10, 0x10001)], eax_mask=0,
         globals=remove_from_groups_globals, max_cases=500),
]


def ui_refresh(rng, pool, args):
    return [(LOGIC_THREAD + 0x84, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4)]

SPECS += [
    Spec("CLogicThread_ClearPendingUiRefresh", 0x0053f530, "logicthread", [], presets=ui_refresh),
]


# The helpers of CLogicThread::ExecuteCommands (src/s3re/game/logic/commands/command_helpers.cpp,
# re/execute_commands.md). Their callees are verified replacements with own
# specs; the ones with world effects are probed (calls compared).
CARRIER_PROBES = {0x0050cf50: (1, "thiscall", "probe"), 0x004def80: (1, "thiscall", "probe")}
CARRIER_JOBS = (10, 11, 12, 13, 80, 169, 170, 9, 14, 79, 81, 168, 171, 0x10a)


def carriers_of_building(rng, pool, args):
    """Settlers 1..count (count 0..30) with owners, the argument's building or another, carrier jobs or
    others; the building a site or not."""
    count = rng.randrange(31)
    fields = [(SETTLER_COUNT, count, 4), (BUILDINGS + args[0] * BUILDING_BYTES + 0x21,
                                          0 if rng.random() < 0.5 else rng.randrange(1, 256), 1)]
    for n in range(1, count + 2):
        s = SETTLERS + n * SETTLER_BYTES
        fields += [(s, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
                   (s + 0x22, args[0] if rng.random() < 0.6 else rng.randrange(0x10000), 2),
                   (s + 0xE, rng.choice(CARRIER_JOBS), 2)]
    return fields


def sites_in_areas(rng, pool, args):
    """Buildings 1..count (count 0..30) of a few owners with doors on a 4 x 4 patch of areas 0..2, sites or
    not, request flags 0/1 or random."""
    count = rng.randrange(31)
    fields = [(BUILDING_COUNT, count, 4)]
    for n in range(1, 32):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, rng.randrange(3), 1), (b + 0xA, rng.randrange(4), 2), (b + 0xC, rng.randrange(4), 2),
                   (b + 0x21, 0 if rng.random() < 0.3 else rng.randrange(1, 10), 1),
                   (b + 0x18, rng.randrange(2) if rng.random() < 0.5 else rng.randrange(256), 1)]
    for x in range(4):
        for y in range(4):
            fields.append((tile(x, y) + 0x14, rng.randrange(3), 2))
    return fields


def pile_for_good_change(rng, pool, args):
    p = PILES + args[0] * PILE_BYTES
    return [(p + 1, 0 if rng.random() < 0.6 else rng.randrange(1, 9), 1),
            (p + 3, 0 if rng.random() < 0.6 else rng.randrange(1, 9), 1),
            (p, 0xFF if rng.random() < 0.4 else rng.randrange(40), 1)]


def pile_good_arg(rng, pool):
    good = 0xFF if rng.random() < 0.4 else rng.randrange(40)
    return good | (rng.getrandbits(24) << 8 if rng.random() < 0.25 else 0)


def soldier_levels(rng, pool, args):
    p = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    return [(p + 0xB4 + 4 * k, (rng.randrange(-1, 4) if rng.random() < 0.85 else rng.getrandbits(32)) & 0xFFFFFFFF, 4)
            for k in (1, 2, 3)]


def owned_tile(rng, pool, args):
    return [(tile(args[0], args[1]) + 8, 0xFF if rng.random() < 0.5 else rng.randrange(20), 1)]


DOCK_MAP = 128
DOCK_REACH = 40   # tiles preset around the chosen tile (walks end at a shore long before)


def dock_coordinate(rng, pool):
    return rng.randrange(45, 84)


def dock_world(rng, pool, args):
    """A 128-tile map; around the chosen tile water (5), shore (0x30) and land in patches, areas 0..2 (half
    0), a few line markers (0x72); the offset table rings 0..9 (91 entries for the candidates, the marker
    check stops at distance 10); the building's door on the patch and its owner."""
    x0, y0 = args[1], args[2]
    fields = [(MAP_SIZE, DOCK_MAP, 4)] + walk_rings(10)
    for x in range(x0 - DOCK_REACH, x0 + DOCK_REACH + 1):
        for y in range(y0 - DOCK_REACH, y0 + DOCK_REACH + 1):
            r = rng.randrange(10)
            terrain = 5 if r < 4 else 0x30 if r < 7 else rng.randrange(64)
            t = tile(x, y)
            fields += [(t + 6, terrain, 1), (t + 0x14, 0 if rng.random() < 0.5 else rng.randrange(1, 3), 2),
                       (t + 7, 0x72 if rng.random() < 0.02 else rng.randrange(100), 1)]
    b = BUILDINGS + args[0] * BUILDING_BYTES
    fields += [(b, rng.randrange(20), 1), (b + 0xA, rng.randrange(x0 - 10, x0 + 11), 2),
               (b + 0xC, rng.randrange(y0 - 10, y0 + 11), 2)]
    return fields


CONDITION_KINDS = tuple(range(53))


def condition_kind(rng, pool):
    return rng.choice(CONDITION_KINDS) if rng.random() < 0.9 else rng.getrandbits(32)

SPECS += [
    Spec("CGameData_RecallSiteCarriers", 0x004dee20, "thiscall", [between(1, 40)],
         presets=carriers_of_building, callees=CARRIER_PROBES, max_cases=800),
    Spec("CGameData_RecallBuildingCarriers", 0x004deec0, "thiscall", [between(1, 40)],
         presets=carriers_of_building, callees=CARRIER_PROBES, max_cases=800),
    Spec("CGameData_RecallCarriersOfOtherSites", 0x004df8e0, "thiscall", [between(1, 31)], eax_mask=0,
         presets=sites_in_areas, callees={0x004dee20: (1, "thiscall", "probe")}, max_cases=800),
    Spec("CGameData_ChangePileGood", 0x004dfb40, "thiscall", [between(1, 1000), pile_good_arg], eax_mask=0,
         presets=pile_for_good_change,
         callees={0x00506080: (1, "thiscall", "probe"), 0x005061b0: (2, "thiscall", "probe")}),
    Spec("CGameData_SetPromotionCost", 0x00508450, "thiscall", [below(20)], presets=soldier_levels),
    Spec("CGameData_OpenDockLineTile", 0x004dbab0, "thiscall", [below(384), below(384), anything], eax_mask=0,
         presets=owned_tile,
         callees={0x0051b640: (2, "thiscall", "probe"), 0x0051b5d0: (3, "thiscall", "probe"),
                  0x0051b770: (2, "thiscall", "probe")}),
    Spec("CGameData_PlaceDockLine", 0x004db710, "thiscall", [between(1, 40), dock_coordinate, dock_coordinate],
         eax_mask=0, presets=dock_world, callees={0x004dbab0: (3, "thiscall", "probe")}, max_cases=300),
    Spec("CGameData_ResetAreaConditionCounters", 0x0051bc20, "thiscall", [below(64), condition_kind],
         case_scale=2),
]


# -- CLogicThread::ExecuteCommands 0x535990 (execute_commands*.cpp) ---------
#
# One spec for the whole function. The fake CLogicThread (LOGIC_THREAD) gets
# a step (+0x3b0) and its two MessageQueues: network -> logic (+0x3c8,
# vtable = emu.FAKE_VTABLE: slot 3 is the wait, probed) holding the case's
# messages, logic -> network (+0x3c4) collecting the pushes. The game's own
# MessageQueue::Pop (0x430980) and Push (0x4309c0) run on them; their
# critical section wrappers (0x60d670 / 0x60d680) are probed, and Push's
# SetEvent call (0x430b5e, `call [SetEvent]`) is replaced by `pop ecx; mov
# eax, 1` for the case (the event handle is pushed, SetEvent would succeed).
# Every case ends with a StepGrant, so the loop ends.
#
# Messages come from EC_COMMANDS: command id -> generator(rng, fields) that
# returns the payload bytes and appends the world fields the command needs
# to `fields`. Each part of the function adds its commands there.

EC_TO_NET = LOGIC_THREAD + 0x1000
EC_FROM_NET = LOGIC_THREAD + 0x1040
EC_EVENT = LOGIC_THREAD + 0x1080
EC_FROM_RING = LOGIC_THREAD + 0x2000
EC_TO_RING = LOGIC_THREAD + 0x3000
EC_RING_DWORDS = 1024
EC_STEP = 0x3b0
EC_SET_EVENT_CALL = 0x00430b5e
EC_SET_EVENT_STUB = int.from_bytes(b"\x59\xb8\x01\x00\x00\x00", "little")  # pop ecx; mov eax, 1
EC_BUILDINGS = 40
EC_SETTLERS = 120
EC_MAP = 384
REFRESH_UI = 0x2d27c
UI_SELECTION_MODE = 0x2d284
UI_PAGE_290 = 0x2d290
UI_PAGE_2A0 = 0x2d2a0
SELECTED_BUILDING = 0x7dfd6c  # g_selectedBuilding
REVEAL_MAP = 0x7dfd84
PLAYER_LISTS = 0x118a47c

EC_COMMANDS: dict = {}
EC_WEIGHTS: dict = {0x12: 6}  # SetTradeRoute: eleven kinds


def ec_hi(v: int) -> int:
    return (v >> 8) & 0xFF


def ec_lo(v: int) -> int:
    return v & 0xFF


def ec_player(rng) -> int:
    return rng.randrange(20) if rng.random() < 0.1 else rng.randrange(4)


def ec_building(rng, fields: list, n: int | None = None) -> int:
    """A building 0..40 with fields the commands look at (on the map, owner 0..3)."""
    n = rng.randrange(EC_BUILDINGS + 1) if n is None else n
    b = BUILDINGS + n * BUILDING_BYTES
    types = (1, 2, 3, 5, 14, 15, 16, 18, 24, 25, 30, 31, 32, 33, 36, 47, 48, 50, 51, 4, 7)
    fields += [(b + 0x00, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
               (b + 0x02, rng.randrange(2, EC_MAP - 2), 2), (b + 0x04, rng.randrange(2, EC_MAP - 2), 2),
               (b + 0x06, rng.choice(types), 1),
               (b + 0x08, 0 if rng.random() < 0.4 else rng.randrange(EC_SETTLERS), 2),
               (b + 0x0a, rng.randrange(EC_MAP), 2), (b + 0x0c, rng.randrange(EC_MAP), 2),
               (b + 0x1f, 0 if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x21, 0 if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x36, rng.randrange(5), 1)]
    return n


def ec_settler(rng, fields: list, n: int | None = None) -> int:
    n = rng.randrange(1, EC_SETTLERS + 1) if n is None else n
    s = SETTLERS + n * SETTLER_BYTES
    fields += [(s + 0x00, 0xFF if rng.random() < 0.15 else rng.randrange(4), 1),
               (s + 0x01, rng.choice((0, 1, 5, 8, 15, 32, 13, 36, 36, 44, 44, 43, 22, 40)), 1),
               (s + 0x04, rng.randrange(10, EC_MAP - 10), 2), (s + 0x06, rng.randrange(10, EC_MAP - 10), 2),
               (s + 0x0e, rng.choice((0x37, 0x35, 0xb0, 0, 1, 0x36)), 2),
               (s + 0x10, rng.randrange(EC_SETTLERS), 2), (s + 0x15, rng.randrange(8), 1)]
    return n


def ec_tile(x: int, y: int) -> int:
    return TILES + (x * 0x300 + y) * 0x18


def ec_area(rng, fields: list) -> int:
    n = rng.randrange(16)
    fields.append((AREAS + n * AREA_BYTES, 0xFF if rng.random() < 0.2 else rng.randrange(4), 1))
    return n


def ec_place_building(rng, fields):
    x, y, player = rng.randrange(EC_MAP), rng.randrange(EC_MAP), ec_player(rng)
    for n in range(1, EC_BUILDINGS + 1):
        if rng.random() < 0.3:
            b = BUILDINGS + n * BUILDING_BYTES
            fields += [(b, player if rng.random() < 0.7 else rng.randrange(4), 1),
                       (b + 0x06, rng.choice((15, 16, 18, 2)), 1),
                       (b + 0x08, 0 if rng.random() < 0.3 else rng.randrange(1, 100), 2),
                       (b + 0x21, 0 if rng.random() < 0.7 else 1, 1)]
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), 0, rng.randrange(60), player]


def ec_scroll(rng, fields):
    return [rng.randrange(256) for _ in range(8)]


def ec_settler_group(rng, fields):
    player = rng.randrange(4)
    count = 0 if rng.random() < 0.2 else rng.randrange(30)
    payload = [player, rng.randrange(30 if rng.random() < 0.25 else 10), ec_hi(count), ec_lo(count)]
    for _ in range(count):
        s = rng.randrange(EC_SETTLERS)
        payload += [ec_hi(s), ec_lo(s)]
    return payload


def ec_building_working(rng, fields):
    n = ec_building(rng, fields)
    return [ec_hi(n), ec_lo(n), 0 if rng.random() < 0.5 else rng.randrange(3), ec_player(rng)]


def ec_work_area(rng, fields):
    n = ec_building(rng, fields, rng.randrange(1, 4) if rng.random() < 0.5 else None)
    b = BUILDINGS + n * BUILDING_BYTES
    x, y = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
    if rng.random() < 0.7:  # near the building, in the same area as its door
        bx, by = rng.randrange(40, EC_MAP - 40), rng.randrange(40, EC_MAP - 40)
        x, y = bx + rng.randrange(-30, 30), by + rng.randrange(-30, 30)
        dx, dy = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
        area = rng.randrange(3)
        fields += [(b + 0x02, bx, 2), (b + 0x04, by, 2), (b + 0x0a, dx, 2), (b + 0x0c, dy, 2),
                   (b + 0x06, rng.choice((2, 30, 14, 25, 36, 24, 5, 3, 51, 4)), 1),
                   (ec_tile(dx, dy) + 0x14, area, 2),
                   (ec_tile(x, y) + 0x14, area if rng.random() < 0.8 else area + 1, 2)]
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(n), ec_lo(n), ec_player(rng)]


def ec_area_setting(kinds: int):
    def generate(rng, fields):
        area = ec_area(rng, fields)
        return [rng.randrange(256) if rng.random() < 0.1 else rng.randrange(kinds),
                rng.randrange(256) if rng.random() < 0.2 else rng.randrange(2), ec_player(rng),
                ec_hi(area), ec_lo(area)]
    return generate


def ec_goods_priority(rng, fields):
    area = ec_area(rng, fields)
    good = rng.randrange(36)
    fields.append((AREAS + area * AREA_BYTES + 0x18a + good, rng.choice((0, 20, 5, 21, 255)), 1))
    return [good, rng.randrange(2), ec_player(rng), ec_hi(area), ec_lo(area)]


def ec_dock(rng, fields):
    n = ec_building(rng, fields)
    b = BUILDINGS + n * BUILDING_BYTES
    if rng.random() < 0.6:
        fields += [(b + 0x06, rng.choice((32, 33)), 1), (b + 0x21, 0, 1), (b + 0x1f, 0, 1)]
    x, y = rng.randrange(EC_MAP), rng.randrange(EC_MAP)
    return [ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(n), ec_lo(n), ec_player(rng)]


def ec_cast_spell(rng, fields):
    player = ec_player(rng)
    s = ec_settler(rng, fields)
    sx, sy = rng.randrange(20, EC_MAP - 20), rng.randrange(20, EC_MAP - 20)
    fields += [(SETTLERS + s * SETTLER_BYTES, player if rng.random() < 0.8 else 5, 1),
               (SETTLERS + s * SETTLER_BYTES + 1, 36 if rng.random() < 0.8 else 44, 1),
               (SETTLERS + s * SETTLER_BYTES + 4, sx, 2), (SETTLERS + s * SETTLER_BYTES + 6, sy, 2)]
    p = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    slot = rng.randrange(8)
    fields += [(p + 4, rng.randrange(4), 4), (p + 0x8c, rng.randrange(100), 4),
               (p + 0x94 + slot * 4, rng.randrange(100), 4)]
    x, y = sx + rng.randrange(-25, 25), sy + rng.randrange(-25, 25)
    return [player, slot, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), ec_hi(s), ec_lo(s)]


def ec_ship(load: bool):
    def generate(rng, fields):
        player = ec_player(rng)
        s = ec_settler(rng, fields)
        base = SETTLERS + s * SETTLER_BYTES
        x, y = rng.randrange(20, EC_MAP - 20), rng.randrange(20, EC_MAP - 20)
        fields += [(base, player if rng.random() < 0.8 else 7, 1), (base + 4, x, 2), (base + 6, y, 2),
                   (base + 0x01, 44 if rng.random() < 0.8 else 43, 1), (base + 0x15, rng.randrange(7), 1)]
        if load:
            # Workshops around the ship, the offset table with growing distances.
            for k in range(60):
                dx, dy = rng.randrange(-5, 6), rng.randrange(-5, 6)
                fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                           (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4),
                           (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 4 if rng.random() < 0.97 else -1 & 0xFFFFFFFF, 4)]
                t = ec_tile(x + dx, y + dy)
                n = rng.randrange(1, EC_BUILDINGS + 1)
                fields += [(t + 0x0a, 0x20 if rng.random() < 0.7 else 0, 1),
                           (t + 0x02, n if rng.random() < 0.9 else 0, 2)]
                if rng.random() < 0.5:
                    b = BUILDINGS + n * BUILDING_BYTES
                    fields += [(b, player, 1), (b + 0x06, rng.choice((31, 47, 48, 50, 33)), 1),
                               (b + 0x36, rng.randrange(4), 1)]
        else:
            fields.append((ec_tile(x, y) + 6, rng.choice((5, 6, 6, 48)), 1))
            fields.append((MAP_SIZE, EC_MAP, 4))
            for d in range(6):  # some shore along the six directions
                for k in range(1, 9):
                    if rng.random() < 0.08:
                        fields.append((ec_tile(x + k * (d % 3 - 1), y + k * (d % 2)) + 6, 48, 1))
            chain = s
            for _ in range(8):  # the passengers' chain (valid settlers)
                nxt = rng.randrange(1, EC_SETTLERS)
                fields.append((SETTLERS + chain * SETTLER_BYTES + 0x10, nxt, 2))
                chain = nxt
        return [player, ec_hi(s), ec_lo(s)]
    return generate


def ec_promote(rng, fields):
    player = ec_player(rng)
    p = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    fields += [(p + 0x90, rng.randrange(120), 4), (p + 0xb4, rng.randrange(80), 4)]
    fields += [(p + 0xb8 + 4 * k, rng.choice((0, 0, 1, 1, 2)), 4) for k in range(3)]
    return [player, rng.choice((1, 2, 3, 0, 4, 0x81, 0x82, 0x83, 0x80, 0xFF))]


def ec_hold_position(rng, fields):
    player, group = rng.randrange(4), rng.randrange(10)
    index = player * 10 + group
    length = rng.randrange(8) if rng.random() < 0.9 else 0
    fields.append((PLAYER_LIST_LENGTHS + index * 4, length, 4))
    for i in range(length):
        s = ec_settler(rng, fields)
        fields.append((PLAYER_LISTS + (index * 100 + i) * 2, s, 2))
    return [player, group]


def ec_storehouse(rng, fields):
    n = ec_building(rng, fields)
    if rng.random() < 0.6:
        fields.append((BUILDINGS + n * BUILDING_BYTES + 6, 1, 1))
    return [ec_hi(n), ec_lo(n), ec_player(rng), rng.randrange(40)]


def ec_site_priority(rng, fields):
    n = ec_building(rng, fields)
    return [ec_hi(n), ec_lo(n), ec_player(rng)]


EC_COMMANDS.update({
    0x01: ec_place_building, 0x02: ec_scroll, 0x03: ec_settler_group, 0x04: ec_building_working,
    0x05: ec_work_area, 0x07: ec_goods_priority, 0x08: ec_area_setting(36), 0x11: ec_area_setting(51),
    0x14: ec_dock, 0x15: ec_cast_spell, 0x16: ec_ship(False), 0x17: ec_dock, 0x18: ec_promote,
    0x19: ec_ship(True), 0x1b: ec_hold_position, 0x1c: ec_storehouse, 0xf1: lambda rng, fields: [],
    0xf5: ec_site_priority, 0xf6: ec_site_priority,
    # Ids without a handler.
    0x00: lambda rng, fields: [rng.randrange(256)], 0x1a: lambda rng, fields: [rng.randrange(256)],
    0xf3: lambda rng, fields: [rng.randrange(256)], 0x80: lambda rng, fields: [rng.randrange(256)],
})

# Network message types other than Command (1) and StepGrant (2): type ->
# generator(rng, fields) returning (byte 1, payload). Types without a
# handler are logged as invalid.
EC_NET_MESSAGES: dict = {
    t: (lambda rng, fields: (rng.randrange(256), [rng.randrange(256)])) for t in (0, 7, 9, 0x20, 0x21, 0x23, 0xFF)
}


def ec_choices() -> tuple:
    """The command ids and network message types to generate. The
    environment variable S3RE_EC_ONLY (e.g. "c9,cd,n3": c = command id, n =
    message type, hex) restricts them, for work on one part of the function."""
    only = os.environ.get("S3RE_EC_ONLY")
    commands, net_types = sorted(EC_COMMANDS), sorted(EC_NET_MESSAGES)
    # Commands with many paths come up more often (EC_WEIGHTS: id -> copies).
    commands = [c for c in commands for _ in range(EC_WEIGHTS.get(c, 1))]
    if only:
        wanted = [item.strip().lower() for item in only.split(",") if item.strip()]
        commands = [c for c in commands if f"c{c:x}" in wanted]
        net_types = [t for t in net_types if f"n{t:x}" in wanted]
    return commands, net_types


def ec_message(type_: int, byte1: int, payload: list) -> list:
    data = [type_, byte1, 0, 0] + list(payload)
    while len(data) % 4:
        data.append(0)
    data[2] = len(data)
    return [int.from_bytes(bytes(data[i:i + 4]), "little") for i in range(0, len(data), 4)]


def ec_queue(offset: int, ring: int, count: int) -> list:
    return [(offset, FAKE_VTABLE, 4), (offset + 0x14, WORLD_BASE + ring, 4),
            (offset + 0x18, EC_RING_DWORDS, 4), (offset + 0x1c, 0, 4), (offset + 0x20, count, 4),
            (offset + 0x24, count, 4), (offset + 0x28, WORLD_BASE + EC_EVENT, 4),
            (offset + 0x2c, WORLD_BASE + EC_EVENT + 0x20, 4), (offset + 0x30, 0, 4)]


def execute_commands_world(rng, pool, args):
    fields = [(BUILDING_COUNT, rng.randrange(EC_BUILDINGS + 1), 4), (MAP_SIZE, EC_MAP, 4),
              (LOCAL_PLAYER, rng.randrange(4), 4), (UI_SELECTION_MODE, rng.choice((0, 1, 2, 3, 4, 4, 5)), 4),
              (UI_PAGE_290, rng.randrange(5), 4), (UI_PAGE_2A0, rng.randrange(5), 4),
              (0x2d288, rng.randrange(2), 4), (0x2d28c, rng.randrange(2), 4), (REFRESH_UI, 0, 4)]
    step = 0 if rng.random() < 0.08 else rng.randrange(1, 1000)
    fields.append((LOGIC_THREAD + EC_STEP, step, 4))
    dwords: list = []
    commands, net_types = ec_choices()
    for _ in range(rng.randrange(5)):
        if net_types and (rng.random() < 0.1 or not commands):
            type_ = rng.choice(net_types)
            byte1, payload = EC_NET_MESSAGES[type_](rng, fields)
            dwords += ec_message(type_, byte1, payload)
        elif commands:
            command = rng.choice(commands)
            dwords += ec_message(1, command, EC_COMMANDS[command](rng, fields))
    dwords += [2 | 8 << 16, (step + rng.randrange(-2, 6)) & 0xFFFFFFFF]  # StepGrant
    fields += [(EC_FROM_RING + 4 * i, d, 4) for i, d in enumerate(dwords)]
    fields += [(LOGIC_THREAD + 0x3c4, WORLD_BASE + EC_TO_NET, 4), (LOGIC_THREAD + 0x3c8, WORLD_BASE + EC_FROM_NET, 4)]
    fields += ec_queue(EC_FROM_NET, EC_FROM_RING, len(dwords)) + ec_queue(EC_TO_NET, EC_TO_RING, 0)
    return fields


def execute_commands_globals(rng, pool, args):
    fields = [(CAMPAIGN_MISSION, 66 if rng.random() < 0.25 else rng.randrange(3), 4),
              (SELECTED_BUILDING, rng.randrange(1, 4) if rng.random() < 0.5 else rng.randrange(EC_BUILDINGS + 2), 4),
              (REVEAL_MAP, 0, 4), (EC_SET_EVENT_CALL, EC_SET_EVENT_STUB, 6)]
    for extra in EC_GLOBALS_EXTRA:
        fields += extra(rng, pool, args)
    return fields


# Callees probed: the simulation functions the commands call (verified on
# their own; recorded with their arguments). NeighbourX/Y, TileDistance and
# IsOnMap run (pure functions of their arguments and the map size).
EC_CALLEES = {
    0x0060d670: (0, "thiscall", "probe"), 0x0060d680: (0, "thiscall", "probe"),  # Enter/LeaveCriticalSection
    0x004d99e0: (5, "thiscall", "probe"), 0x004ff3f0: (0, "thiscall", "probe"), 0x0051b4d0: (0, "thiscall", "probe"),
    0x004dfb40: (2, "thiscall", "probe"), 0x004df490: (1, "thiscall", "probe"), 0x004df510: (1, "thiscall", "probe"),
    0x004df590: (1, "thiscall", "probe"), 0x004df610: (1, "thiscall", "probe"), 0x004df690: (1, "thiscall", "probe"),
    0x004df730: (1, "thiscall", "probe"), 0x004df7d0: (1, "thiscall", "probe"), 0x004df850: (1, "thiscall", "probe"),
    0x004dee20: (1, "thiscall", "probe"), 0x004deec0: (1, "thiscall", "probe"), 0x004df8e0: (1, "thiscall", "probe"),
    0x00523840: (1, "thiscall", "probe"), 0x004ff3c0: (1, "thiscall", "probe"), 0x0051bc20: (2, "thiscall", "probe"),
    0x004dbb90: (1, "thiscall", "probe"), 0x004db710: (3, "thiscall", "probe"), 0x00508450: (1, "thiscall", "probe"),
    0x004fefd0: (10, "thiscall", "probe"), 0x0050ce80: (3, "thiscall", "probe"),
}

# Blocks only reachable through a C++ exception: the catch of Win32::CTimeout*
# (0x53b823; the probed wait never throws; in-process tests throw one).
# Dead: 0x53b81e, the `jmp` after the loop's `xor eax, eax; test eax, eax; je`.
EC_EXEMPT = [0x0053b823, 0x0053b82d, 0x0053b833, 0x0053b81e]
# Virtual calls (slot offset -> as Spec.virtuals); slot 0xc is the queue's wait.
EC_VIRTUALS = {0xc: 2}
# More S3.EXE globals per case: functions (rng, pool, args) -> [(address, value, size)].
EC_GLOBALS_EXTRA: list = []

SPECS += [
    Spec("CLogicThread_ExecuteCommands", 0x00535990, "logicthread", [], presets=execute_commands_world,
         globals=execute_commands_globals, virtuals=EC_VIRTUALS, callees=EC_CALLEES, exempt_blocks=EC_EXEMPT,
         # The epilogue (0x53b839, where the catch continues) is a separate
         # start in the function list; the jump tables follow at 0x53b850.
         cover_until=0x0053b850,
         # About 45 command ids and message types share the cases.
         case_scale=16,
         # Full coverage takes 24233 cases (1025 s alone in Release,
         # 2026-11-24): quick runs do 900 cases (about 38 s), --full runs
         # (32000) cover all 985 blocks.
         quick_cases=900),
]


# -- ExecuteCommands: SetTradeRoute 0x12, SetWorkshopProduction 0x13 (execute_commands_trade.cpp)
#
# Harbours (33) and marketplaces (34) with a Trade record 0..31 whose door,
# work tile and route tiles share walk regions / areas often; workshops (31,
# 47, 48, 50) and shipyards (32) idle or running. NeighbourX/Y run (pure).
# The UI's special menu (+0x2d298) is often on the trade (4..13) or
# workshop (14..16) pages.

EC_TRADE_RECORDS = 32
UI_SPECIAL_MENU = 0x2d298


def ec_trade_tile_box(fields: list, x: int, y: int, region: int, area: int, radius: int) -> None:
    for tx in range(x - radius, x + radius + 1):
        for ty in range(y - radius, y + radius + 1):
            if 0 <= tx < EC_MAP and 0 <= ty < EC_MAP:
                fields += [(ec_tile(tx, ty) + 0x10, region, 1), (ec_tile(tx, ty) + 0x14, area, 2)]


def ec_trade_building(rng, fields: list, types: tuple) -> int:
    n = rng.randrange(1, 4) if rng.random() < 0.5 else rng.randrange(EC_BUILDINGS + 1)
    b = BUILDINGS + n * BUILDING_BYTES
    fields += [(b + 0x06, rng.choice(types), 1), (b + 0x00, rng.randrange(4) if rng.random() < 0.9 else 0xFF, 1),
               (b + 0x21, 0 if rng.random() < 0.85 else rng.randrange(1, 256), 1)]
    fields.append((UI_SPECIAL_MENU, rng.randrange(18) if rng.random() < 0.8 else rng.getrandbits(32), 4))
    return n


def ec_trade_route(rng, fields):
    n = ec_trade_building(rng, fields, (33, 33, 34, 34, 32, 2))
    b = BUILDINGS + n * BUILDING_BYTES
    trade = rng.randrange(EC_TRADE_RECORDS)
    region, area = rng.randrange(1, 3), rng.randrange(3)
    door = (rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10))
    work = (rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10))
    x, y = rng.randrange(10, EC_MAP - 10), rng.randrange(10, EC_MAP - 10)
    fields += [(b + 0x34, trade, 2), (b + 0x0a, door[0], 2), (b + 0x0c, door[1], 2),
               (b + 0x30, work[0], 2), (b + 0x32, work[1], 2),
               (b + 0x22, 0xFFFF if rng.random() < 0.15 else rng.randrange(6), 2)]
    same = rng.random() < 0.7
    ec_trade_tile_box(fields, door[0], door[1], region, area, 0)
    ec_trade_tile_box(fields, work[0], work[1], region, area, 3)  # the water two steps from the work tile
    ec_trade_tile_box(fields, x, y, region if same else region + 1, area if same else area + 1,
                      1 if rng.random() < 0.5 else 0)
    if rng.random() < 0.1:
        fields.append((ec_tile(x, y) + 0x10, 0, 1))  # no walk region (kind 7, harbour)
    t = BUILDING_EXTRAS + trade * BUILDING_EXTRA_BYTES
    for point in range(4):
        if rng.random() < 0.5:  # the same tile, the same column, or anywhere
            px, py = rng.choice(((x, y), (x, y + 1), (rng.randrange(EC_MAP), rng.randrange(EC_MAP))))
            fields += [(t + 2 + 2 * point, px, 2), (t + 0x0a + 2 * point, py, 2)]
    for k in range(40):  # the offset table: growing distances (kind 7, harbour)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randrange(-4, 5) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randrange(-4, 5) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 2, 4)]
    kind = rng.randrange(13) if rng.random() < 0.9 else rng.randrange(256)
    if 8 <= kind <= 11:
        line = rng.randrange(34) if rng.random() < 0.9 else rng.randrange(40)
        if rng.random() < 0.5:
            fields.append((t + 0x12 + rng.randrange(36), rng.choice((0, 50, 99, 100, 255)), 1))
        return [ec_player(rng), kind, ec_hi(n), ec_lo(n), line, rng.choice((0, 1, 10, 60, 100, 255)), 0, 0]
    return [ec_player(rng), kind, ec_hi(n), ec_lo(n), ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y)]


def ec_workshop_production(rng, fields):
    n = ec_trade_building(rng, fields, (31, 47, 48, 50, 32, 32, 33))
    b = BUILDINGS + n * BUILDING_BYTES
    player = ec_player(rng)
    race = rng.randrange(4)
    fields += [(PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 4, race, 4),
               (b + 0x00, player if rng.random() < 0.8 else rng.randrange(4), 1),
               (b + 0x1f, rng.choice((0, 0, 0x10, 0x21, 0x32, 0x40, 0x03)), 1),
               (b + 0x20, 0xFF if rng.random() < 0.5 else rng.randrange(256), 1),
               (b + 0x22, 0xFFFF if rng.random() < 0.2 else rng.randrange(6), 2)]
    value = 0 if rng.random() < 0.3 else (rng.choice((0x10, 0x20, 0x30, 0x40, 0x50, 0)) |
                                          (race if rng.random() < 0.8 else rng.randrange(16)))
    return [player, value, ec_hi(n), ec_lo(n)]


EC_COMMANDS.update({0x12: ec_trade_route, 0x13: ec_workshop_production})
EC_CALLEES.update({
    0x005236b0: (1, "thiscall", "probe"),  # CLogicThread::DrawBuildingRouteOverlay (verified)
    0x004dda10: (1, "thiscall", "probe"),  # CGameData::ReleaseBuildingExtraTiles (verified)
})


# -- ExecuteCommands: area settings, garrison, carriers (execute_commands_areas.cpp)
# Commands 0x0a ChangeTransportOrder, 0x0b ChangeToolSettings, 0x0c
# ChangeGarrison, 0x10 ConvertCarriers.

EC_SETTLER_GROUP_COUNTS = 0x7ad620  # g_settlerGroups.counts
EC_CURRENT_SETTLER_GROUP = 0x7dfd68  # g_currentSettlerGroup


def ec_owned_area(rng, fields: list, player: int) -> int:
    n = rng.randrange(16)
    owner = player if rng.random() < 0.85 else rng.choice((0xFF, (player + 1) % 4))
    fields.append((AREAS + n * AREA_BYTES, owner, 1))
    return n


def ec_transport_order(rng, fields):
    player = rng.randrange(4)
    area = ec_owned_area(rng, fields, player)
    base = AREAS + area * AREA_BYTES
    order = list(range(1, 37))
    rng.shuffle(order)
    for g in range(36):
        value = order[g] if rng.random() < 0.9 else rng.choice((0, 255, order[g]))
        fields += [(base + 0x1f6 + g, value, 1), (base + 0x21a + g, rng.randrange(3), 1)]
    fields.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 4, rng.randrange(5), 4))
    if rng.random() < 0.5:  # the local player looks at the transport page
        fields += [(LOCAL_PLAYER, player, 4), (UI_SELECTION_MODE, 2, 4), (UI_PAGE_290, rng.choice((4, 4, 3)), 4)]
    good =rng.randrange(256) if rng.random() < 0.05 else rng.randrange(36)
    return [good, rng.randrange(8), player, ec_hi(area), ec_lo(area)]


def ec_tool_settings(rng, fields):
    player = rng.randrange(4)
    area = ec_owned_area(rng, fields, player)
    base = AREAS + area * AREA_BYTES
    fields += [(base + 0x23e + k, 5 * rng.randrange(15), 1) for k in range(3)]
    fields += [(base + 0x242 + k, rng.randrange(101), 1) for k in range(3)]
    if rng.random() < 0.5:  # the local player looks at the player menu
        fields += [(LOCAL_PLAYER, player, 4), (UI_SELECTION_MODE, 3, 4), (UI_PAGE_2A0, rng.choice((2, 3, 1)), 4)]
    value =0 if rng.random() < 0.35 else rng.randrange(30 if rng.random() < 0.5 else 256)
    return [rng.randrange(12), value, player, ec_hi(area), ec_lo(area)]


EC_SOLDIER_TYPES = (5, 32, 35, 8, 30, 33, 15, 31, 34, 0, 40)


def ec_garrison(rng, fields):
    player = rng.randrange(4)
    n = rng.randrange(1, EC_BUILDINGS + 1)
    b = BUILDINGS + n * BUILDING_BYTES
    if rng.random() < 0.85:  # a tower or castle of the player
        kind = rng.choice((15, 16, 18))
        size = {15: 1, 16: 3, 18: 6}[kind]
        swords = rng.randrange(size + 1)
        pikes = rng.randrange(size - swords + 1)
        fields += [(b, player, 1), (b + 6, kind, 1), (b + 0x1a, swords, 1), (b + 0x20, pikes, 1),
                   (b + 0x1c, rng.randrange(5), 1)]
        # The garrison chain (settlers 100..106; an unknown type continues with settler 0's next).
        length = rng.randrange(7)
        fields.append((b + 8, 100 if length else 0, 2))
        for k in range(length):
            s = SETTLERS + (100 + k) * SETTLER_BYTES
            fields += [(s + 1, rng.choice(EC_SOLDIER_TYPES), 1), (s + 0x10, 0 if k + 1 == length else 101 + k, 2)]
        fields += [(SETTLERS + 0x10, 0 if rng.random() < 0.5 else 110, 2), (SETTLERS + 110 * SETTLER_BYTES + 1, 5, 1),
                   (SETTLERS + 110 * SETTLER_BYTES + 0x10, 0, 2)]
    else:  # another player's building (the original keeps the sizes of an earlier command for the player's own)
        fields.append((b, (player + 1) % 4, 1))
    return [ec_hi(n), ec_lo(n), player, rng.randrange(10)]


def ec_convert_carriers(rng, fields):
    player = rng.randrange(4)
    kind = rng.randrange(6)
    x, y = rng.randrange(40, EC_MAP - 40), rng.randrange(40, EC_MAP - 40)
    for i in range(3000):
        dx, dy = (rng.randrange(-6, 7), rng.randrange(-6, 7)) if i < 40 else (0, 0)
        fields += [(NEIGHBOUR_OFFSETS + i * 16, dx & 0xFFFFFFFF, 4), (NEIGHBOUR_OFFSETS + i * 16 + 4, dy & 0xFFFFFFFF, 4)]
        if i < 40:
            t = ec_tile(x + dx, y + dy)
            s = rng.randrange(1, EC_SETTLERS)
            base = SETTLERS + s * SETTLER_BYTES
            fields += [(t, s if rng.random() < 0.8 else 0, 2), (t + 8, player if rng.random() < 0.85 else 0xFF, 1),
                       (base, player if rng.random() < 0.85 else 0xFF, 1),
                       (base + 1, rng.choice((0, 0, 40, 40, 22)), 1),
                       (base + 0x0e, rng.choice((1, 2, 0x37, 0x35, 3)), 2)]
    fields.append((ec_tile(x, y), 0, 2))  # offsets 40..2999 all look at (x, y): empty
    return [kind, player, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y), rng.randrange(40 if rng.random() < 0.3 else 6)]


def ec_group_globals(rng, pool, args):
    return [(EC_SETTLER_GROUP_COUNTS, rng.randrange(50), 2), (EC_CURRENT_SETTLER_GROUP, rng.randrange(10), 4)]


EC_COMMANDS.update({0x0a: ec_transport_order, 0x0b: ec_tool_settings, 0x0c: ec_garrison, 0x10: ec_convert_carriers})
EC_CALLEES.update({
    0x0050cf50: (1, "thiscall", "probe"),  # ResetSettlerToIdle
    0x005428e0: (2, "thiscall", "probe"),  # CLogicThread::RemoveSettlerFromGroups
    0x00542990: (1, "thiscall", "probe"),  # CLogicThread::UpdateGroupMenu
})
EC_GLOBALS_EXTRA.append(ec_group_globals)


# -- ExecuteCommands: MoveUnits 0x09, 0x0d, 0x0e, 0x0f (execute_commands_units.cpp) --
#
# A group of up to 12 units of the player around a target; the tiles around
# the target hold enemy buildings / units for the attack search, the offset
# table has growing distances and small steps. SetSettlerTarget (0x503610)
# is probed; TileDistance runs.

EC_UNIT_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35, 22, 23, 36, 40, 41, 41, 41, 43, 44, 44, 0, 42)
EC_UNIT_JOBS = (212, 213, 302, 303, 55, 53, 142, 176, 0)


def ec_move_units(rng, fields):
    player, group = rng.randrange(4), rng.randrange(10)
    index = player * 10 + group
    x, y = rng.randrange(30, EC_MAP - 30), rng.randrange(30, EC_MAP - 30)
    if rng.random() < 0.05:
        x = rng.randrange(EC_MAP + 40)
    elif rng.random() < 0.1:  # next to the map's last row: offsets reach past it
        y = EC_MAP - rng.randrange(1, 4)
    fields.append((SETTLER_COUNT, EC_SETTLERS, 4))
    for p in range(20):
        base = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        fields += [(base, 2 if rng.random() < 0.25 else 1, 4), (base + 4, rng.randrange(4), 4),
                   (base + 0x0c, rng.randrange(3), 4)]
    for k in range(400):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randrange(-8, 9) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randrange(-8, 9) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 8, k // 6, 4)]
    # Tiles around the target: some settlers and buildings, terrain, regions.
    for dx in range(-24, 25):
        for dy in range(-24, 25):
            tx, ty = x + dx, y + dy
            if not (0 <= tx < EC_MAP and 0 <= ty < EC_MAP):
                continue
            t = ec_tile(tx, ty)
            fields += [(t, rng.randrange(1, EC_SETTLERS + 1) if rng.random() < 0.1 else 0, 2),
                       (t + 0x02, rng.randrange(1, EC_BUILDINGS + 1), 2),
                       (t + 0x06, rng.choice((5, 5, 6, 16, 32, 48, 0x11)), 1),
                       (t + 0x0a, 0x20 if rng.random() < 0.05 else 0, 1),
                       (t + 0x10, rng.randrange(2), 1), (t + 0x14, rng.randrange(2), 2),
                       (t + 0x16, rng.randrange(2), 2)]
    for n in range(1, EC_BUILDINGS + 1):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, rng.randrange(5), 1), (b + 0x06, rng.choice((15, 16, 18, 2, 33)), 1),
                   (b + 0x02, rng.randrange(EC_MAP), 2), (b + 0x04, rng.randrange(EC_MAP), 2)]
    catapults = rng.random() < 0.3
    for n in range(1, EC_SETTLERS + 1):
        s = SETTLERS + n * SETTLER_BYTES
        fields += [(s, rng.randrange(5), 1), (s + 0x01, rng.choice(EC_UNIT_TYPES), 1),
                   (s + 0x04, rng.randrange(EC_MAP), 2), (s + 0x06, rng.randrange(EC_MAP), 2),
                   (s + 0x0e, rng.choice(EC_UNIT_JOBS), 2),
                   (s + 0x1a, 0 if rng.random() < 0.7 else 1, 1),
                   (s + 0x1c, 0xFFFF if rng.random() < 0.5 else rng.randrange(EC_MAP), 4),
                   (s + 0x24, rng.randrange(EC_MAP), 2), (s + 0x26, rng.randrange(EC_MAP), 2)]
        fields += [(s + 0x30 + 2 * c, 0xFFFF if rng.random() < 0.5 else rng.randrange(EC_MAP), 2)
                   for c in range(6)]
    # The unit's own tiles must be on the map: their regions are compared.
    length = 0 if rng.random() < 0.1 else rng.randrange(13)
    fields.append((PLAYER_LIST_LENGTHS + index * 4, length, 4))
    for i in range(length):
        n = rng.randrange(1, EC_SETTLERS + 1) if rng.random() < 0.95 else EC_SETTLERS + 1 + rng.randrange(4)
        fields.append((PLAYER_LISTS + (index * 100 + i) * 2, n, 2))
        if n <= EC_SETTLERS:
            s = SETTLERS + n * SETTLER_BYTES
            fields += [(s, player if rng.random() < 0.9 else rng.randrange(4), 1),
                       (s + 0x01, 41 if catapults and rng.random() < 0.94 else rng.choice(EC_UNIT_TYPES), 1),
                       (s + 0x04, x + rng.randrange(-20, 21), 2), (s + 0x06, y + rng.randrange(-20, 21), 2)]
    return [player, group, ec_hi(x), ec_lo(x), ec_hi(y), ec_lo(y)]


EC_COMMANDS.update({0x09: ec_move_units, 0x0d: ec_move_units, 0x0e: ec_move_units, 0x0f: ec_move_units})
EC_CALLEES.update({0x00503610: (3, "thiscall", "probe")})  # SetSettlerTarget
# 0x536faa: the clamp of the offset index to 9900, dead (the loop stops at 5000).
EC_EXEMPT.extend([0x00536faa])


# -- ExecuteCommands: DestroyBuilding, multiplayer / UI commands, network
# messages (execute_commands_net.cpp) ------------------------------------------
#
# The texts run as they are: sprintf, std::string, the wide-string helpers
# and the UI's message list (0x51e440, writing the thread's list at +0x414,
# 8 entries here, emptied per case). Probed: the graphics text lookup
# (0x5faaa0, returns the narrow text "Desync!" 0x73021c, which has no
# conversions), CrushBuilding, the game-over / player-list helpers, the
# multiplayer save functions, the world dump, CComputerPlayer's constructor
# (sometimes 0, to reach the assertion, which is probed too), and the window
# message of FilmEnd. Checksums and stamps of SyncCheck are chosen together
# with S3.EXE's checksum table (EC_NET_PENDING_GLOBALS, set by the message
# generators and written by the globals function of the same case).

EC_NET_PENDING_GLOBALS: list = []
EC_SYNC_CHECKSUMS = 0x7deb58  # g_syncChecksums[20][8]
EC_SIMULATION_PAUSED = 0x7ace62
EC_SYNC_RANDOM_HISTORY = 0x7dfb58
EC_GAME_TICK = 0x7dfd48
EC_UI_LIST = 0x414          # CLogicThread: the UI message list (0x1c per entry)
EC_UI_LIST_COUNT = 0x644
EC_UI_LIST_INDEX = 0x648


EC_PLAYER_SETUP = SCRATCH + 5 * SCRATCH_SLOT_BYTES  # what the probed 0x4351e0 returns


def ec_net_state(rng, fields: list) -> int:
    """Players, objectives, save state, the thread's UI message list, the
    UI feedback ring (PushUiFeedback) and the player setup object 0x5db260
    copies the (empty) name from. Returns the number of active players."""
    fields += [(0x15394ac, rng.randrange(10), 4), (EC_PLAYER_SETUP + 0x18, 0, 4), (EC_PLAYER_SETUP + 0x1c, 0, 4), (EC_PLAYER_SETUP + 0x20, 0, 4),
               (EC_PLAYER_SETUP + 0x24, 0, 4)]
    fields += [(LOGIC_THREAD + EC_UI_LIST_COUNT, 8, 4), (LOGIC_THREAD + EC_UI_LIST_INDEX, rng.randrange(8), 4)]
    for k in range(8):
        entry = LOGIC_THREAD + EC_UI_LIST + k * 0x1c
        fields += [(entry + 4, 0, 4), (entry + 8, 0, 4), (entry + 0xc, 0, 4)]
    active = 0
    for p in range(20):
        record = PLAYER_RECORDS + p * PLAYER_RECORD_BYTES
        state = 1 if rng.random() < 0.6 else rng.randrange(3)
        active += state == 1
        fields += [(record, state, 4),
                   (record + 0x1c, (p + 3) % 20 if rng.random() < 0.8 else rng.randrange(20), 4)]
    fields += [(0x1195dcc, rng.randrange(2), 1), (0x2d250, rng.randrange(2), 1), (0x15394b0, rng.randrange(2), 1),
               (LOGIC_THREAD + 0x224, 0 if rng.random() < 0.3 else rng.randrange(4000, 6000), 4),
               (LOGIC_THREAD + 0x228, rng.randrange(3), 4), (LOGIC_THREAD + 0x22c, rng.randrange(6), 4),
               (0xe115dc, rng.randrange(1, 60), 4), (0x110a578, rng.randrange(1, 60), 4),
               (0x1238858, rng.randrange(1, 60), 4), (SETTLER_COUNT, rng.randrange(1, 60), 4)]
    return active


def ec_net_player(rng) -> int:
    return rng.randrange(20) if rng.random() < 0.15 else rng.randrange(4)


def ec_net_simple(rng, fields):
    ec_net_state(rng, fields)
    return [ec_net_player(rng)]


def ec_save_done(rng, fields):
    active = ec_net_state(rng, fields)
    if rng.random() < 0.6:  # the last missing acknowledgement
        fields.append((LOGIC_THREAD + 0x22c, active - 1, 4))
    return [ec_net_player(rng)]


def ec_destroy_building(rng, fields):
    ec_net_state(rng, fields)
    n = ec_building(rng, fields)
    b = BUILDINGS + n * BUILDING_BYTES
    fields.append((b + 6, rng.choice((41, 43, 45, 55, 15, 2)), 1))
    return [ec_hi(n), ec_lo(n), ec_net_player(rng)]


def ec_player_lost(rng, fields):
    ec_net_state(rng, fields)
    player = ec_net_player(rng)
    for n in range(1, EC_BUILDINGS + 1):
        b = BUILDINGS + n * BUILDING_BYTES
        fields += [(b, player if rng.random() < 0.5 else rng.randrange(4), 1), (b + 6, rng.choice((15, 16, 18, 2)), 1)]
    return [player]


def ec_sync_check(rng, fields):
    ec_net_state(rng, fields)
    player = rng.randrange(4)
    table = [rng.randrange(4) for _ in range(7)]
    stamp_now = rng.choice((0, 5, 6))
    for k, value in enumerate(table):
        EC_NET_PENDING_GLOBALS.append((EC_SYNC_CHECKSUMS + (player * 8 + k) * 4, value, 4))
    EC_NET_PENDING_GLOBALS.append((EC_SYNC_CHECKSUMS + (player * 8 + 7) * 4, stamp_now, 4))
    EC_NET_PENDING_GLOBALS.append((EC_SIMULATION_PAUSED, 0 if rng.random() < 0.8 else 1, 2))
    payload = [player, rng.randrange(256)]
    for k in range(7):
        value = table[k] if rng.random() < 0.7 else rng.randrange(4)
        payload += list(value.to_bytes(4, "big"))
    stamp = rng.choice((stamp_now, stamp_now, stamp_now + 1, max(stamp_now - 1, 0), 0))
    return payload + list(stamp.to_bytes(4, "big"))


def ec_check_version(rng, fields):
    ec_net_state(rng, fields)
    index = rng.randrange(32)
    word = rng.randrange(4)
    EC_NET_PENDING_GLOBALS.append((EC_SYNC_RANDOM_HISTORY + index * 2, rng.randrange(4), 2))
    return [ec_hi(index), ec_lo(index), ec_hi(word), ec_lo(word), ec_net_player(rng)]


def ec_net_message(type_: int):
    def generate(rng, fields):
        ec_net_state(rng, fields)
        net_id = rng.randrange(22)
        if type_ in (3, 5):
            return net_id, []
        if type_ == 4:
            return 0, []
        if type_ == 6:  # a wide text at message byte 12
            return 0, [net_id, 1, 2, 3, 4, 5, 6, 7, ord("H"), 0, ord("i"), 0, 0, 0]
        if type_ == 8:
            return 0, [net_id, rng.randrange(256)]
        payload = [0] * (6 + 4 * 20 + 2)  # 0x22: lag words at message bytes 6 and 10 + 4 * id
        for at in range(2, len(payload) - 1, 4):
            lag = rng.choice((0, 4000, 5000, 5001, 60000))
            payload[at], payload[at + 1] = lag & 0xFF, lag >> 8
        return 0, payload
    return generate


def ec_net_globals(rng, pool, args):
    fields = [(EC_GAME_TICK, rng.randrange(5000, 10000), 4)] + EC_NET_PENDING_GLOBALS
    EC_NET_PENDING_GLOBALS.clear()
    return fields


EC_COMMANDS.update({
    0x06: ec_destroy_building, 0xf2: ec_net_simple, 0xf4: ec_sync_check, 0xf7: ec_net_simple,
    0xf8: ec_save_done, 0xf9: ec_net_simple, 0xfa: ec_net_simple, 0xfb: ec_check_version,
    0xfc: ec_player_lost, 0xfd: ec_net_simple, 0xfe: ec_net_simple, 0xff: ec_net_simple,
})
EC_NET_MESSAGES.update({t: ec_net_message(t) for t in (3, 4, 5, 6, 8, 0x22)})
EC_GLOBALS_EXTRA.append(ec_net_globals)
EC_CALLEES.update({
    0x005faaa0: (2, "thiscall", lambda h: 0x0073021c),   # CGraphicsData text lookup -> "Desync!"
    0x004d9250: (1, "thiscall", "probe"),                # CrushBuilding (verified)
    0x00540500: (1, "thiscall", "probe"), 0x005411b0: (0, "thiscall", "probe"),  # UI / players (not ours)
    0x0050ac40: (2, "thiscall", "probe"), 0x0050af70: (0, "thiscall", "probe"),  # multiplayer save
    0x0050b3c0: (0, "thiscall", "probe"),
    0x004ff6d0: (3, "thiscall", "probe"),                # desync dump file
    0x00550900: (4, "thiscall", lambda h: 0 if h % 8 == 0 else 0x5000 + (h >> 8) % 20),  # CComputerPlayer
    0x00610960: (1, "stdcall", "probe"),                 # Win32::ThrowOutOfMemory (fastcall text, file; line)
    0x0060f480: (3, "thiscall", "probe"),                # post a window message (FilmEnd)
    # The game setup's player (lobby code with vectors that reallocate): a fake object.
    0x004351e0: (1, "thiscall", lambda h: WORLD_BASE + EC_PLAYER_SETUP),
})
# Dead: the other branches of the std::string code the original inlines in
# 0x06 and 0xf7 (string(buffer) through two temporaries that share one
# buffer, then its destructor). The temporaries are released while the
# result still shares the buffer (count > 0), so their `delete` branches
# (0x53746a, 0x5374a3, 0x53a8e1, 0x53a91a) never run; when the result is
# destroyed its count is 0, so the unsharing (0x53750b, 0x53750f) and the
# count decrement (0x53758d, 0x537591) don't run either, and _Grow(0) in its
# erase returns false (0x537570).
EC_EXEMPT.extend([0x0053746a, 0x005374a3, 0x0053750b, 0x0053750f, 0x00537570, 0x0053758d, 0x00537591,
                  0x0053a8e1, 0x0053a91a])
_ec_spec = next(s for s in SPECS if s.ours == "CLogicThread_ExecuteCommands")
_ec_spec.new_fails = 0.15        # only the computer player's block (0x780 bytes) fails: 0x53afba, 0x53b201
_ec_spec.new_fails_size = 0x780

# The texts' CRT functions (sprintf, mbstowcs in 0x60b840, their locale
# locks) call InterlockedIncrement / InterlockedDecrement and
# Enter/LeaveCriticalSection, which the emulator doesn't implement: for the
# case, those import slots point at single-threaded equivalents in three
# padding gaps of S3.EXE (0x624aa1, 0x627ec2: `mov ecx, [esp+4]; inc/dec
# dword [ecx]; mov eax, [ecx]; ret 4`; 0x62c181: `ret 4`), and the CRT's
# _lock / _unlock (0x624a2b / 0x624a8c, cdecl) return at once (the first
# _lock of a lock number would allocate its critical section with HeapReAlloc).
EC_INTERLOCKED_INCREMENT = 0x00624aa1
EC_INTERLOCKED_DECREMENT = 0x00627ec2
EC_LOCK_NOTHING = 0x0062c181


def ec_interlocked_stubs(rng, pool, args):
    return [(EC_INTERLOCKED_INCREMENT, int.from_bytes(bytes.fromhex("8b4c2404ff018b01c20400"), "little"), 11),
            (EC_INTERLOCKED_DECREMENT, int.from_bytes(bytes.fromhex("8b4c2404ff098b01c20400"), "little"), 11),
            (EC_LOCK_NOTHING, int.from_bytes(bytes.fromhex("c20400"), "little"), 3),
            (0x00655244, EC_INTERLOCKED_INCREMENT, 4), (0x00655240, EC_INTERLOCKED_DECREMENT, 4),
            (0x006550fc, EC_LOCK_NOTHING, 4), (0x00655128, EC_LOCK_NOTHING, 4),  # import slots
            (0x00624a2b, 0xc3, 1), (0x00624a8c, 0xc3, 1)]  # _lock, _unlock: ret


EC_GLOBALS_EXTRA.append(ec_interlocked_stubs)


# --- CLogicThread::LoadSaveGame (load_save_game.cpp) ---
# The savegame is modelled in Python: GetChunk (0x4a60f0) is probed and its
# effect returns a chunk buffer in unused world space (one slot per chunk
# key; contents lazily random) after writing the fields that steer the
# load: the version (all gates), a map size of 0..5, small table
# high-water marks, tiles of the rows that refer to economy sectors 1..7
# (so the eco drop sees used and unused sectors), the victory conditions'
# protect flag. ReleaseChunk (0x4a6310), UnblockCastleTiles (0x4dfa60,
# verified on its own) and the player names' assign (0x413d70, verified on
# its own) are probed: their calls and arguments are compared.
LSG_CHUNKS = 0x1540000
LSG_SLOTS = {0: 0x0, 1: 0x1000, 4: 0xa000, 6: 0xf000, 7: 0x11000, 8: 0x12000, 9: 0x13000, 13: 0x14000,
             14: 0x15000, 10: 0x16000, 11: 0xa4000, 12: 0xba000, 15: 0xc4000, 16: 0xc7000, 0x18: 0xde000,
             0x19: 0xdf000, 0x1c: 0xe0000, 0x1d: 0x10e000, 0x1f: 0x10f000}
LSG_VERSIONS = (0x50, 0x81, 0x82, 0x85, 0x86, 0x88, 0x89, 0x95, 0x96, 0x98, 0x99, 0xa0)


def _lsg_chunk(m, ecx, args, h):
    """CMapFile::GetChunk(type, sub) on the fake savegame."""
    chunk_type, sub = args[0] & 0xFFFF, args[1] & 0xFFFF
    if chunk_type == 2:
        offset = 0x3000 + 0x3000 * sub
    elif chunk_type == 3:
        offset = 0x9000 + 0x100 * sub
    else:
        offset = LSG_SLOTS[chunk_type]
    base = WORLD_BASE + LSG_CHUNKS + offset

    def put(at, value, size):
        m.guest_write(base + at, value.to_bytes(size, "little"))

    if chunk_type == 0:
        put(8, LSG_VERSIONS[h % len(LSG_VERSIONS)], 4)
    elif chunk_type == 1:
        put(0x18, h % 6, 4)
    elif chunk_type == 3:
        size = struct.unpack("<I", m.guest_read(WORLD_BASE + MAP_SIZE, 4))[0]
        for y in range(min(size, 8)):
            pick = (h >> (3 * y)) % 4
            area = 0 if pick == 0 else (h >> (2 * y + 20)) % 8 if pick < 3 else (h >> y) & 0xFFFF
            put(8 + 24 * y + 0x14, area, 2)
    elif chunk_type == 4:
        put(0xc, h % 8, 4)
    elif chunk_type in (7, 8, 9, 13, 14):
        put(8, h % 6, 4)
    elif chunk_type == 0x18:
        put(8 + 0x44, h & 1, 1)
    return base


# About 14 s per case (Release): every case copies ~1 MB of fixed-size
# chunks (diggertab 578 KB, ProdStat 184 KB, ...), tracked byte by byte. 20
# cases cover all 40 blocks; the main evidence is the in-process tests
# (every savegame of the installation and synthetic ones, whole world).
SPECS += [
    Spec("CLogicThread_LoadSaveGame", 0x00508fa0, "logicthread", [anything], eax_mask=0, max_cases=40,
         quick_cases=20,
         callees={0x004a60f0: (2, "thiscall", "probe", _lsg_chunk),
                  0x004a6310: (2, "thiscall", "probe"),
                  0x004dfa60: (0, "thiscall", "probe"),
                  0x00413d70: (1, "thiscall", "this")}),
]


# -- session 54 (logic-misc): small CLogicThread members (logic_thread_members.cpp) --

CURRENT_GROUP = 0x007dfd68
GROUP_CHANGED = 0x007ad5f8


def message_lifetimes(rng, pool, args):
    """The 20 message lines' lifetimes around 0, sometimes any value."""
    return [(LOGIC_THREAD + 0x414 + line * 0x1c + 0x10,
             rng.choice([0, 1, 2, -1 & 0xFFFFFFFF, rng.randrange(1000), rng.getrandbits(32)]), 4)
            for line in range(20)]


def group_store_state(rng, pool, args):
    """All ten groups (lists of a few settlers, often shared between groups,
    sometimes empty), the current group (sometimes -1 or the argument), the
    change flags and the selected building; the world part: the settlers'
    owner and type bytes and the special menu (UpdateGroupMenu)."""
    world = [(0x2d2d4, rng.randrange(4), 4), (0x2d298, rng.randrange(8), 4)]
    exe = []
    shared = [rng.randrange(1, 0x8000) for _ in range(6)]
    for group in range(10):
        count = 0 if rng.random() < 0.25 else rng.randrange(1, 12) if rng.random() < 0.9 else rng.randrange(101)
        exe.append((GROUP_COUNTS + group * 2, count, 2))
        for k in range(count):
            settler = rng.choice(shared) if rng.random() < 0.6 else rng.randrange(0x8000)
            exe.append((GROUP_SETTLERS + group * 200 + k * 2, settler, 2))
            world.append((SETTLERS + settler * 0x40 + 1, rng.choice([0x24, 0x2c, 0x17, 0x02]), 1))
        exe.append((GROUP_CHANGED + group * 4, rng.randrange(3), 4))
    current = 0xFFFFFFFF if rng.random() < 0.1 else args[0] if args and rng.random() < 0.15 else rng.randrange(10)
    exe += [(CURRENT_GROUP, current, 4),
            (SELECTED_BUILDING, 0 if rng.random() < 0.5 else rng.randrange(5000), 4)]
    return world, exe


GROUP_STORE_EXE: list = []


def group_store_presets(rng, pool, args):
    world, exe = group_store_state(rng, pool, args)
    GROUP_STORE_EXE[:] = exe
    return world


def group_store_globals(rng, pool, args):
    return list(GROUP_STORE_EXE)


def transport_order_area(rng, pool, args):
    """The argument's area (when below 0x1000): free or a player's, its
    transport order list with small goods numbers, sometimes the asked one."""
    area, good = args
    if area >= 0x1000:
        return []
    base = AREAS + area * 0x2e0
    fields = [(base, 0xFF if rng.random() < 0.2 else rng.randrange(20), 1)]
    for p in range(36):
        fields.append((base + 0x1f6 + p, rng.randrange(40) if rng.random() < 0.8 else rng.getrandbits(8), 1))
    if rng.random() < 0.5 and good < 256:
        fields.append((base + 0x1f6 + rng.randrange(36), good, 1))
    return fields


def area_number(rng, pool):
    return rng.choice([rng.randrange(0x1000), rng.randrange(0x1000), 0x1000, 0xfff, rng.getrandbits(32)])


SPECS += [
    Spec("CLogicThread_Setup", 0x0053c970, "logicthread", [anything] * 10),
    Spec("CLogicThread_ClearMessages", 0x0051e650, "logicthread", [], presets=message_lifetimes),
    # EAX: the original's points past the last line (callers don't use it).
    Spec("CLogicThread_AgeMessages", 0x0051e680, "logicthread", [], eax_mask=0, presets=message_lifetimes),
    Spec("CLogicThread_ClearSelection", 0x00542630, "logicthread", [], presets=group_store_presets,
         globals=group_store_globals),
    # EAX: the empty-group path leaves the caller's (callers don't use it);
    # the in-process test compares it on the other paths.
    Spec("CLogicThread_StoreSettlerGroup", 0x00542820, "logicthread", [below(10)], eax_mask=0,
         presets=group_store_presets, globals=group_store_globals),
    Spec("CLogicThread_GoodsPriorityPosition", 0x00542d40, "logicthread", [area_number, below(40, 0.1)],
         presets=transport_order_area),
    Spec("LogicRecord24_Construct_args", 0x005437e0, "thiscall", [anything] * 9),
]


# -- session 54 (logic-misc): TutorialWaitDone, PickHitArea (ui_state.cpp) --

def tutorial_wait(rng, pool, args):
    return [(0x2d304, rng.choice([0, 0, 1, 2, rng.getrandbits(32)]), 4)]


HIT_AREAS = 0x7ade08
PICK_STATE: list = []


def pick_hit_area_world(rng, pool, args):
    """Draws the hit areas for the globals (run after the presets): a count
    of 0, negative or up to 60, half 0/1, entries near (x, y) or anywhere."""
    x, y = args
    x = x - (1 << 32) if x & 0x80000000 else x
    y = y - (1 << 32) if y & 0x80000000 else y
    kind = rng.randrange(8)
    count = 0 if kind == 0 else ((-1 - rng.randrange(5)) & 0xFFFFFFFF) if kind == 1 else rng.randrange(1, 60)
    half = rng.randrange(2)
    exe = [(0x7dfd5c, count, 4), (0x7dfd64, half, 4)]
    for k in range(count if count < 0x80000000 else 0):
        entry = HIT_AREAS + (2 * k + half) * 10
        close = rng.random() < 0.3
        ex = (x + rng.randint(-50, 50)) if close else rng.getrandbits(16)
        ey = (y + rng.randint(-50, 50)) if close else rng.getrandbits(16)
        exe += [(entry + 2, ex & 0xFFFF, 2), (entry + 4, ey & 0xFFFF, 2),
                (entry + 6, rng.getrandbits(16), 2), (entry + 8, rng.getrandbits(16), 2)]
    PICK_STATE[:] = exe
    return []


def pick_hit_area_globals(rng, pool, args):
    return list(PICK_STATE)


SPECS += [
    Spec("CGameData_TutorialWaitDone", 0x00515b00, "thiscall", [], presets=tutorial_wait),
    Spec("CGameData_PickHitArea", 0x004ff0a0, "thiscall", [between(-50, 1100), between(-50, 800)], eax_mask=0,
         presets=pick_hit_area_world, globals=pick_hit_area_globals, max_cases=1000),
]


# -- session 54 (logic-misc): SelectSettlerGroup, SelectSettlersOfType (logic_thread_members.cpp) --

def _trunc_div(a, b):
    """Signed 32-bit division truncating toward zero."""
    a = a - (1 << 32) if a & 0x80000000 else a
    q = abs(a) // b
    return (q if a >= 0 else -q) & 0xFFFFFFFF


def screen_centre_tile(camera_x, camera_y, size, width, height):
    """CGameData::ScreenCenterTileX/Y (ui_state.cpp) in Python."""
    row = _trunc_div((camera_y + 0x640) & 0xFFFFFFFF, 18)
    rows = lambda shift: ((_trunc_div((height + 0xff) & 0xFFFFFFFF, 9) + 0xb) & 0xFFFFFFFF) >> shift
    skew = ((size + ((100 - row) & 0xFFFFFFFF) * 2) & 0xFFFFFFFF) >> 1
    half = ((_trunc_div((width + 0x17) & 0xFFFFFFFF, 16) + 0xa) & 0xFFFFFFFF) >> 1
    column = _trunc_div((camera_x - 0xd0) & 0xFFFFFFFF, 16)
    x = (column + rows(2) - skew + half) & 0xFFFFFFFF
    y = (rows(1) + row * 2 - 200) & 0xFFFFFFFF
    return x, y


SELECT_EXE: list = []
SELECT_SCREEN = (504, 480)


def select_group_state(rng, pool, args):
    """The groups (group_store_state) with every listed settler's owner and
    position on a small map (CenterCameraOnTile reads its tile)."""
    world, exe = group_store_state(rng, pool, args)
    size = rng.randrange(32, 200)
    local = rng.randrange(4)
    world += [(MAP_SIZE, size, 4), (0x2d2d4, local, 4)]
    for address, value, width in list(exe):
        if GROUP_SETTLERS <= address < GROUP_SETTLERS + 2000 and width == 2:
            s = SETTLERS + value * 0x40
            world += [(s, local if rng.random() < 0.6 else rng.randrange(4), 1),
                      (s + 4, rng.randrange(size), 2), (s + 6, rng.randrange(size), 2)]
    if rng.random() < 0.6 and args:  # the same group again (camera jump)
        exe.append((CURRENT_GROUP, args[0], 4))
    SELECT_EXE[:] = exe + [(SCREEN_WIDTH, SELECT_SCREEN[0], 4), (SCREEN_HEIGHT, SELECT_SCREEN[1], 4)]
    return world


def select_globals(rng, pool, args):
    return list(SELECT_EXE)


def select_type_state(rng, pool, args):
    """A map whose screen-centre tile is known (camera and view size fixed per
    case), a ring table of small offsets, settlers of a few owners and the
    four types 0..3 on the tiles around the centre, and the groups."""
    type_, mode = args
    world, exe = group_store_state(rng, pool, [0])
    size = rng.randrange(48, 160)
    local = rng.randrange(4)
    camera_x, camera_y = rng.randrange(0, 2500), rng.randrange(0, 1200)
    cx, cy = screen_centre_tile(camera_x, camera_y, size, *SELECT_SCREEN)
    world += [(MAP_SIZE, size, 4), (0x2d2d4, local, 4), (0x2d25c, camera_x, 4), (0x2d260, camera_y, 4)]
    n = 0
    d = 0
    pool_settlers = [rng.randrange(1, 32000) for _ in range(150)]
    while n < 400:
        for dx in range(-d, d + 1):
            for dy in range(-d, d + 1):
                if max(abs(dx), abs(dy)) != d or n >= 400:
                    continue
                world += [(0x110a588 + n * 16, dx & 0xFFFFFFFF, 4), (0x110a588 + n * 16 + 4, dy & 0xFFFFFFFF, 4)]
                x, y = (cx + dx) & 0xFFFFFFFF, (cy + dy) & 0xFFFFFFFF
                if x < size and y < size:
                    settler = rng.choice(pool_settlers) if rng.random() < 0.5 else 0
                    world.append((TILES + (x * 0x300 + y) * 24, settler, 2))
                n += 1
        d += 1
    for s in pool_settlers:
        world += [(SETTLERS + s * 0x40, local if rng.random() < 0.7 else rng.randrange(4), 1),
                  (SETTLERS + s * 0x40 + 1, type_ if rng.random() < 0.6 else rng.randrange(4), 1)]
    SELECT_EXE[:] = exe + [(SCREEN_WIDTH, SELECT_SCREEN[0], 4), (SCREEN_HEIGHT, SELECT_SCREEN[1], 4)]
    return world


def select_mode(rng, pool):
    return rng.choice([1, 2, 3, 1, 2, 0, 4, rng.getrandbits(32)])


SPECS += [
    # EAX: the empty-group path leaves the caller's (not used).
    Spec("CLogicThread_SelectSettlerGroup", 0x00542660, "logicthread", [below(10)], eax_mask=0,
         presets=select_group_state, globals=select_globals),
    # EAX not used by the callers (an invalid mode leaves the caller's).
    Spec("CLogicThread_SelectSettlersOfType", 0x00542b60, "logicthread", [below(4), select_mode], eax_mask=0,
         presets=select_type_state, globals=select_globals, max_cases=300),
]


# --- CLogicThread::SaveGame (save_game.cpp) ---
# The file is not written: the CMapFile members SaveGame calls (all verified
# on their own) are probed, with the bytes they are given: WriteChunk the
# whole chunk buffer (heap blocks of the emulator's allocator, so the
# bytes SaveGame leaves unwritten are the same lazily random ones in both
# runs and compared as well), WriteChunkWithTail the header part and the
# tail's size and address, WriteChunkData size and address, Open the path's
# text. The CMapFile lives in SaveGame's frame, so these probes don't
# record ECX ("stdcall": the same stack arguments, ECX left out) and a
# stack address in an argument is recorded as "stack". Also probed: the
# CRT's _mkdir (the path's text) and time, GameSettings::MapName (its
# effect fills the result string: empty, unshared, frozen or shared
# buffer), GameSettings::SaveSetup (verified on its own) and AI::SaveGlobals (the
# original). GetError returns 0, 1 or a failure, so both results occur.
#
# The emulator's heap (emu.HEAP_SIZE, 0x30000 bytes) can't hold the
# 0x8d63a-byte diggertab buffer: that operator new returns null and every
# case ends in its out-of-memory throw. So the spec covers SaveGame up to
# chunk 10; the earlier out-of-memory paths are reached by an effect of the
# probe that precedes each allocation (SG_FAIL_AFTER picks one per case).
# The blocks after chunk 10's allocation are in COVERED_ELSEWHERE.
SG_NAME = SCRATCH + 0x8000          # the name argument (a std::string) and its text
SG_FOLDER_TEXT = SCRATCH + 0x8100   # g_gameFolder's text
SG_PLAYER_NAMES = SCRATCH + 0x8200  # 20 player name texts, 0x80 bytes each
SG_MAP_NAME = SCRATCH + 0x9200      # MapName's result text
SG_FAIL_AFTER = LOGIC_THREAD + 0x6f0  # which probe call makes the next operator new fail (fake thread, unused)
SG_PLAYER_INFO_NAME_PTRS = 0x007acfb4  # g_playerInfo[i].name.ptr, 0x44 apart
SG_GAME_FOLDER = 0x007a7c80
SG_STACK = (0x00200000, 0x00300000)  # emu.STACK_BASE, + STACK_SIZE
SG_EXE = []

# The probe call after which the next chunk buffer's allocation fails, and
# that buffer's size (chunks 0, 1, 2, 4, 6, 7, 8, 9; only that size, so the
# strings ThrowOutOfMemory builds still get memory); anything else: no
# forced failure.
SG_TRIGGERS = ((("open",), 0x120), (("setup",), 0x1860), (("chunk", 1), 0x10), (("tail", 2, 1), 0x3118),
               (("tail", 4), 0x19fc), (("chunk", 6), 0xc), (("tail", 7), 0xc), (("tail", 8), 0xc))


def _sg_text(m, address, limit=0x200):
    return m.guest_read(address, limit).split(b"\0", 1)[0]


def _sg_stack(value):
    return "stack" if SG_STACK[0] <= value < SG_STACK[1] else value


def _sg_dword(m, address):
    return struct.unpack("<I", m.guest_read(address, 4))[0]


def _sg_trigger(m, event):
    index = _sg_dword(m, WORLD_BASE + SG_FAIL_AFTER)
    if index < len(SG_TRIGGERS) and SG_TRIGGERS[index][0] == event:
        m.new_fails = True
        m.new_fails_size = SG_TRIGGERS[index][1]


def _sg_construct(m, ecx, args, h):
    """The first probe call of each run: the original's run may have set
    new_fails (run.py sets it once per case, before both runs)."""
    m.new_fails = False
    m.new_fails_size = None


def _sg_record_open(m, ecx, args):
    path, mode = args
    pointer, length = _sg_dword(m, path + 4), _sg_dword(m, path + 8)
    return (bytes(m.guest_read(pointer, length)) if pointer else b"", length, mode)


def _sg_open(m, ecx, args, h):
    _sg_trigger(m, ("open",))


def _sg_record_chunk(m, ecx, args):
    chunk_type, sub, buffer = args
    size = _sg_dword(m, buffer + 4)
    return (chunk_type, sub, bytes(m.guest_read(buffer, min(size, 0x20000))))


def _sg_chunk(m, ecx, args, h):
    _sg_trigger(m, ("chunk", args[0]))


def _sg_record_tail(m, ecx, args):
    chunk_type, sub, buffer, tail, tail_at = args
    size = _sg_dword(m, buffer + 4)
    head = (size - tail) & 0xFFFFFFFF
    return (chunk_type, sub, bytes(m.guest_read(buffer, min(head, 0x4000))), size, tail, tail_at)


def _sg_tail(m, ecx, args, h):
    _sg_trigger(m, ("tail", args[0], args[1]))
    _sg_trigger(m, ("tail", args[0]))


def _sg_map_name(m, ecx, args, h):
    """GameSettings::MapName(result): the result string."""
    result = args[0]
    kind = h % 4
    text_at = WORLD_BASE + SG_MAP_NAME
    if kind == 0:
        m.guest_write(result, struct.pack("<IIII", 0, 0, 0, 0))
        return result
    length = (h >> 8) % 60
    text = bytes(0x41 + (h >> (16 + i % 16)) % 26 for i in range(length))
    count = {1: 0, 2: 0xFF, 3: 1 + (h >> 40) % 3}[kind]  # unshared, frozen, shared
    m.guest_write(text_at, bytes([count]) + text + b"\0")
    m.guest_write(result, struct.pack("<IIII", 0, text_at + 1, length, length | 31))
    return result


def _sg_record_setup(m, ecx, args):
    return (_sg_stack(args[0]), args[1], args[2] & 0xFFFF)


def _sg_setup(m, ecx, args, h):
    _sg_trigger(m, ("setup",))


SG_CALLEES = {
    0x004a5a70: (1, "stdcall", "probe", _sg_construct),                    # CMapFile::CMapFile
    0x00621556: (1, "cdecl", "probe", None, lambda m, ecx, args: (_sg_text(m, args[0]),)),  # _mkdir
    0x004a5c80: (2, "stdcall", "probe", _sg_open, _sg_record_open),        # CMapFile::Open
    0x0061f6a7: (1, "cdecl", "probe"),                                     # time
    0x00436a60: (1, "thiscall", "probe", _sg_map_name, lambda m, ecx, args: (_sg_stack(args[0]),)),
    0x00433490: (3, "thiscall", "probe", _sg_setup, _sg_record_setup),    # GameSettings::SaveSetup
    0x004a6540: (3, "stdcall", "probe", _sg_chunk, _sg_record_chunk),      # WriteChunk
    0x004a6600: (5, "stdcall", "probe", _sg_tail, _sg_record_tail),        # WriteChunkWithTail
    0x004a66d0: (4, "stdcall", "probe"),                                   # WriteChunkData
    0x00551220: (1, "thiscall", "probe", None, lambda m, ecx, args: (_sg_stack(args[0]),)),
    0x004a60d0: (0, "stdcall", lambda h: (0, 0, 1, 0x80004005)[h % 4]),    # GetError
    0x004a5f40: (0, "stdcall", "probe"),                                   # Close
    0x004a5b50: (0, "stdcall", "probe"),                                   # ~CMapFile
}


def save_game_state(rng, pool, args):
    fields = [(SG_FAIL_AFTER, rng.randrange(len(SG_TRIGGERS) + 3), 4),
              (MAP_SIZE, rng.choice([0, 1, 2, 3, 5]), 4),
              (0x2d2d4, rng.randrange(20), 4),  # local_player
              (0xe115dc, rng.choice([0, 3, rng.getrandbits(32)]), 4)]  # area_high_water (only sizes)
    for offset in (0x12a5e5c, 0x1499e60, 0x1238858, 0x110a578, 0x67440):  # the tables' high-water marks
        fields.append((offset, rng.choice([0, 7, rng.getrandbits(32)]), 4))
    for i in range(20):  # computer players
        fields.append((LOGIC_THREAD + 0x350 + 4 * i, 0 if rng.random() < 0.6 else rng.getrandbits(32) | 1, 4))
    # The name argument: a string with or without text.
    name = bytes(0x61 + rng.randrange(26) for _ in range(rng.randrange(1, 30)))
    empty = rng.random() < 0.2
    fields += [(SG_NAME, 0, 4), (SG_NAME + 4, 0 if empty else WORLD_BASE + SG_NAME + 0x21, 4),
               (SG_NAME + 8, 0 if empty else len(name), 4), (SG_NAME + 12, len(name) | 31, 4),
               (SG_NAME + 0x20, 0, 1)]
    for i, byte in enumerate(name + b"\0"):
        fields.append((SG_NAME + 0x21 + i, byte, 1))
    for i, byte in enumerate(b"C:\\Games\\S3\\\0"):
        fields.append((SG_FOLDER_TEXT + 1 + i, byte, 1))
    folder = WORLD_BASE + SG_FOLDER_TEXT + 1 if rng.random() < 0.5 else 0
    exe = [(SG_GAME_FOLDER, 0, 4), (SG_GAME_FOLDER + 4, folder, 4), (SG_GAME_FOLDER + 8, 12 if folder else 0, 4),
           (SG_GAME_FOLDER + 12, 31 if folder else 0, 4)]
    for player in range(20):
        text = bytes(0x41 + rng.randrange(26) for _ in range(rng.randrange(0x7f)))
        at = SG_PLAYER_NAMES + player * 0x80
        for i, byte in enumerate(text + b"\0"):
            fields.append((at + i, byte, 1))
        exe.append((SG_PLAYER_INFO_NAME_PTRS + player * 0x44, WORLD_BASE + at if rng.random() < 0.8 else 0, 4))
    SG_EXE[:] = exe
    return fields


def save_game_globals(rng, pool, args):
    return SG_EXE


# Dead blocks: the inlined destructors (erase(), then _Tidy(true)) of the
# path temporaries g_gameFolder + "Save", + "Save\Multi", + "Save\\" and
# that + name: each is a fresh result of operator+ (never empty, its buffer
# unshared: count byte 0), so the null-text, shared-buffer and frozen
# branches never run; erase(0, npos) leaves length 0, so _Grow(0) returns
# false and the length checks against max_size (std::_Xlen) never fail.
# The same for the map name's copy (MapName's result, all four kinds of
# buffer reached by the probe's effect): after erase() its length is 0, and
# a shared buffer was split first, so the final _Tidy sees count 0 or 0xff.
SG_DEAD_STRING_BLOCKS = (
    0x5099d5, 0x509a01, 0x509a06, 0x509a62, 0x509a86, 0x509a8b,  # g_gameFolder + "Save"
    0x509ab4, 0x509adf, 0x509ae4, 0x509b40, 0x509b66, 0x509b6a,  # g_gameFolder + "Save\Multi"
    0x509c67, 0x509c7b, 0x509c7f, 0x509c83, 0x509ca8, 0x509cae,  # (g_gameFolder + "Save\\") + name
    0x509cbc, 0x509cd4, 0x509cd9,
    0x509d15, 0x509d1a, 0x509d45, 0x509d65, 0x509da6, 0x509dba,  # g_gameFolder + "Save\\"
    0x509dbf, 0x509dc3, 0x509ddf, 0x509de5, 0x509def, 0x509e13, 0x509e17,
    0x509f95, 0x509fae, 0x509fb2, 0x509fd4, 0x509fda, 0x509fe8, 0x50a00f,  # the map name
)

SPECS += [
    # Returns a bool in AL (the caller reads AL only); every case throws at
    # chunk 10 at the latest (see above). About 0.3 s per case (Release).
    Spec("CLogicThread_SaveGame", 0x00509980, "logicthread", [lambda rng, pool: WORLD_BASE + SG_NAME],
         eax_mask=0xFF, presets=save_game_state, globals=save_game_globals, callees=SG_CALLEES, max_cases=300,
         exempt_blocks=SG_DEAD_STRING_BLOCKS),
]


# --- CLogicThread::SaveGameToSlot (save_game_to_slot.cpp) ---
# SaveGame 0x509980 (verified on its own) is probed: the name it gets is
# recorded as text, length and capacity (the string lives in the caller's
# frame) and its result is random, so both results reach the caller. The
# game's sprintf runs; GameSettings::GameGuid runs on g_gameSettings, whose
# GUID (+0xb0) is random per case; UuidToStringA / RpcStringFreeA are API
# probes (emu.py: the GUID bytes, the text pointer).
STS_GUID = 0x007a7ac8  # g_gameSettings + 0xb0


def _sts_record_save(m, ecx, args):
    pointer, length, capacity = struct.unpack("<III", m.guest_read(args[0] + 4, 12))
    return (_sg_stack(args[0]), bytes(m.guest_read(pointer, length)) if pointer else b"", length, capacity)


def save_slot_globals(rng, pool, args):
    return [(STS_GUID + 4 * i, rng.getrandbits(32), 4) for i in range(4)]


def save_slot(rng, pool):
    roll = rng.random()
    if roll < 0.7:
        return rng.randrange(12)
    return rng.randrange(-200, 1000) & 0xFFFFFFFF if roll < 0.85 else rng.getrandbits(32)


SPECS += [
    # Dead blocks: 0x50adbd and 0x50af24, the _Eos(0) of the inlined erase()
    # of the name: _Grow(0, false) returns false (the length is 0 then).
    Spec("CLogicThread_SaveGameToSlot", 0x0050ac40, "logicthread",
         [one_of(0, 1, 0x100, 0xff01, 0x12345600), save_slot], eax_mask=0xFF, globals=save_slot_globals,
         callees={0x00509980: (1, "thiscall", lambda h: h & 0x1ff, None, _sts_record_save)},
         exempt_blocks=(0x50adbd, 0x50af24)),
]


# --- CGameData::TileAtScreenPoint (ui_state.cpp, session 55) ---
# The map size (small, sometimes 0..2), the drawn camera position (around the
# map, sometimes far off but within a million pixels: further out the walk's
# row offset wraps and both read outside the world), the display mode and the
# sidebar width; tile heights are lazily random.
def tile_at_screen_world(rng, pool, args):
    size = rng.choice([0, 1, 2]) if rng.random() < 0.05 else rng.randrange(4, 65)
    far = lambda: rng.randrange(-1000000, 1000000) & 0xFFFFFFFF
    camera_x = far() if rng.random() < 0.1 else rng.randrange(-200, size * 16 + 600) & 0xFFFFFFFF
    camera_y = far() if rng.random() < 0.1 else rng.randrange(-200, size * 9 + 300) & 0xFFFFFFFF
    return [(MAP_SIZE, size, 4), (0x2d254, camera_x, 4), (0x2d258, camera_y, 4)]


def tile_at_screen_globals(rng, pool, args):
    return [(0x00730f08, rng.randrange(5), 4),  # g_displayMode
            (0x00730f24, rng.choice([136, 176, 216, 0, 0xFFFFFFFB]), 4)]  # g_sidebarWidth


def screen_coordinate(low, high):
    def generate(rng, pool):
        if rng.random() < 0.05:
            return rng.randrange(-1000000, 1000000) & 0xFFFFFFFF
        return rng.randrange(low, high) & 0xFFFFFFFF
    return generate


SPECS += [
    Spec("CGameData_TileAtScreenPoint", 0x004ff170, "thiscall", [screen_coordinate(-50, 1100),
                                                                 screen_coordinate(-50, 800)],
         eax_mask=0, presets=tile_at_screen_world, globals=tile_at_screen_globals),
]


# --- DrawData::DrawData and its string element constructor (draw_data.cpp, session 55) ---
# The object is the world (0xc7720 bytes, lazily random). The CRT's `eh vector
# constructor iterator` 0x61ff7f and LogicRecord24's constructor run as they
# are. ignore_bytes: the allocator byte of the string at +0xc66c0 is an
# uninitialized local of the original's frame (ours writes 0); the
# self-test pairs WrongDrawDataConstruct, which differs only in that
# string's pointer (+0xc66c4), next to it.
DRAW_DATA_ALLOCATOR = 0xc66c0

SPECS += [
    Spec("DrawData_Construct", 0x00523480, "thiscall", [], ignore_bytes=(DRAW_DATA_ALLOCATOR,), max_cases=200),
    # ECX anywhere in the scratch area (the allocator byte is ECX's high byte).
    Spec("String_EmptyHighByteAllocator", 0x00523540, "fastcall",
         [lambda rng, pool: WORLD_BASE + SCRATCH + rng.randrange(0x1000), anything]),
]
SELF_TEST += [
    Spec("WrongDrawDataConstruct", 0x00523480, "thiscall", [], ignore_bytes=(DRAW_DATA_ALLOCATOR,), max_cases=20),
]


# -- CGameData::ChooseNewGameObjectives (objectives.cpp, session 57) ---------
# Objectives on or off, the RNG state and the seven objective bytes lazily
# random; the NGO time in g_gameSettings (+0xd0) random.
def ngo_choose_state(rng, pool, args):
    return [(0x1195dcc, rng.choice([0, 1, 1, 1, rng.getrandbits(8)]), 1)]


def ngo_choose_globals(rng, pool, args):
    return [(0x007a7a18 + 0xd0, rng.getrandbits(32), 4), (0x007deb54, rng.getrandbits(8), 1)]


SPECS += [
    # A random RNG state can sit in a short cycle that never draws seven
    # usable ids (both versions loop forever): such cases end at max_instructions.
    Spec("CGameData_ChooseNewGameObjectives", 0x004ffbc0, "thiscall", [], eax_mask=0, presets=ngo_choose_state,
         globals=ngo_choose_globals, max_instructions=300_000),
]


# -- wip/logic-r4: the CD music (logic_music.cpp) -----------------------------
# The installed CD (0x7a7c18) and the music switch (g_config +0x5e) random;
# the thread's music fields small (a large playlist reads outside the
# image in both versions); CCDAudio Play/Stop probed (verified on their own).
MUSIC_CALLEES = {
    0x00615ea0: (2, "thiscall", "probe"),  # Win32::CCDAudio::Play(from, to)
    0x00615f70: (0, "thiscall", "probe"),  # Win32::CCDAudio::Stop
}


def music_globals(rng, pool, args):
    return [(0x007a7c18, rng.choice([0, 1, 2, 3, 4, 5, 6, rng.getrandbits(32)]), 4),
            (0x007a7af0 + 0x5e, rng.choice([0, 1, 1, 1, rng.getrandbits(8)]), 1)]


def music_thread(rng, pool, args):
    t = LOGIC_THREAD
    return [(t + 0x654, rng.choice([0, 0, 0, 1, rng.getrandbits(8)]), 1),
            (t + 0x658, rng.randrange(-2, 6) & 0xFFFFFFFF, 4),
            (t + 0x65c, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(-2, 6) & 0xFFFFFFFF]), 4),
            (t + 0x660, rng.randrange(-1, 5) & 0xFFFFFFFF, 4)]


SPECS += [
    Spec("CLogicThread_PlayNextMusicTrack", 0x0053f5f0, "logicthread", [], eax_mask=0, presets=music_thread,
         globals=music_globals, callees=MUSIC_CALLEES),
    Spec("CLogicThread_StartMusic", 0x0053f580, "logicthread",
         [lambda rng, pool: rng.choice([0xFFFFFFFF, rng.randrange(-2, 6) & 0xFFFFFFFF])], eax_mask=0,
         presets=music_thread, globals=music_globals, callees=MUSIC_CALLEES),
    Spec("CLogicThread_StopMusic", 0x0053f5c0, "logicthread", [], eax_mask=0, presets=music_thread,
         globals=music_globals, callees=MUSIC_CALLEES),
]


# -- wip/logic-r4: CLogicThread::FinishGame (finish_game.cpp) ----------------
# The player info table (0x7acfa8, 20 x 0x44 bytes) gets random ranks (+4)
# and fields (+0x18..+0x40; equal scores in some cases), the world players
# random active flags; PostThreadMessage probed (verified on its own).
def finish_game_globals(rng, pool, args):
    # One write of the whole table (one write_image per global is slow); the
    # names (+0x08..+0x17) keep their zero image bytes.
    table = bytearray(0x44 * 20)
    equal = rng.random() < 0.3
    for p in range(20):
        base = 0x44 * p
        struct.pack_into("<I", table, base + 4, rng.choice([0, 0, 0, rng.randrange(1, 21), rng.getrandbits(32)]))
        for k in range(11):
            value = 1 if equal and k >= 3 else rng.choice([rng.randrange(50), rng.getrandbits(32)])
            struct.pack_into("<I", table, base + 0x18 + 4 * k, value)
    return [(0x007acfa8, int.from_bytes(table, "little"), len(table))]


def finish_game_state(rng, pool, args):
    fields = [(LOGIC_THREAD + 0x234, rng.choice([0, 0, 0, 1, rng.getrandbits(8)]), 1)]
    for p in range(20):
        fields.append((0x11943dc + 0x148 * p, rng.choice([0, rng.getrandbits(32) | 1]), 4))
    return fields


SPECS += [
    Spec("CLogicThread_FinishGame", 0x0053e720, "logicthread", [], eax_mask=0, presets=finish_game_state,
         globals=finish_game_globals, callees={0x0060f480: (3, "thiscall", "probe")}),
]


# -- wip/logic-r4: CLogicThread::PublishDrawData (draw_data_publish.cpp) -----
# The shared slot (+0x3cc) points into the scratch area (InterlockedExchange
# is an API probe that stores); CEvent::Set probed.
def publish_draw_data_state(rng, pool, args):
    return [(LOGIC_THREAD + 0x3cc, WORLD_BASE + SCRATCH + 4 * rng.randrange(64), 4)]


SPECS += [
    Spec("CLogicThread_PublishDrawData", 0x0051f0e0, "logicthread", [], presets=publish_draw_data_state,
         callees={0x0042cf80: (0, "thiscall", "probe")}),
]


# -- wip/logic-r4: CLogicThread's destructors (logic_thread_dtor.cpp) --------
# ECX = THREAD_SELF (framework.py; the object + 4). The timer (+0x3bc, its
# +4 part) and the world (+0x3a0) are 0 or FAKE_VTABLE objects; the chat
# wstring's characters (+0x3fc) 0 or a world buffer with a random refcount
# byte; the waitable list (+0x3e0) has entries or not: get(0) is probed and
# hands out a link (whose waitable is 0 or a FAKE_VTABLE object at its +4)
# or empties the list (count +8 = 0, returns 0); the list's slot 1
# (0x5b7ff0, through the game's vtable), the CRT's vector destructor
# iterator, the list and object code and ~CGameThread are probed.
LT_OBJECTS = 0x12000


def lt_get_link(machine, ecx, args, h):
    if h % 3 == 0:
        machine.guest_write(ecx + 8, b"\0\0\0\0")
        return 0
    link = WORLD_BASE + LT_OBJECTS + 0x300 + 8 * ((h >> 4) & 1)
    waitable = 0 if (h >> 8) % 4 == 0 else WORLD_BASE + LT_OBJECTS + 0x400
    machine.guest_write(link + 4, struct.pack("<I", waitable))
    return link


def lt_parts(rng, pool, args):
    full = THREAD_SELF_OFFSET - 4
    fields = [(full + 0x3bc, rng.choice([0, WORLD_BASE + LT_OBJECTS]), 4), (LT_OBJECTS + 4, FAKE_VTABLE, 4),
              (full + 0x3a0, rng.choice([0, WORLD_BASE + LT_OBJECTS + 0x100]), 4), (LT_OBJECTS + 0x100, FAKE_VTABLE, 4),
              (LT_OBJECTS + 0x404, FAKE_VTABLE, 4),
              (full + 0x3e0 + 8, rng.choice([0, 1, rng.getrandbits(32)]), 4)]
    if rng.random() < 0.7:
        fields += [(full + 0x3fc, WORLD_BASE + LT_OBJECTS + 0x202, 4),
                   (LT_OBJECTS + 0x201, rng.choice([0, 0, 0xff, 1, 2, rng.getrandbits(8)]), 1)]
    else:
        fields.append((full + 0x3fc, 0, 4))
    return fields


SPECS += [
    # Dead block 0x53c905: the list base's "still has entries" branch (the
    # loop before it runs until the count +8 is 0, and clearAndDestroy is
    # probed).
    Spec("CLogicThread_Destroy", 0x0053c7d0, "fastcall", [thread_self, anything], presets=lt_parts,
         callees={0x0061f18a: (4, "stdcall", "probe"), 0x0060b500: (1, "thiscall", "probe", lt_get_link),
                  0x005b7ff0: (1, "thiscall", "probe"), 0x0060b560: (0, "thiscall", "probe"),
                  0x0059f200: (0, "thiscall", "probe"), 0x0060b5a0: (0, "thiscall", "probe"),
                  0x00610330: (0, "thiscall", "probe"), 0x005b60a0: (0, "thiscall", "probe")},
         virtuals={0: 1}, exempt_blocks=(0x0053c905,)),
    Spec("CLogicThread_DeletingDestroy", 0x0053c7a0, "fastcall", [thread_self, anything, below(4)],
         callees={0x0053c7d0: (0, "thiscall", "probe")}),
]


# -- wip/logic-r4: ShowMessage / ChatInputKey (message_lines.cpp) ------------
# The 20 message lines' texts, the chat text and the argument get wide
# string buffers in the world (MSG_BUFFERS + 0x100 * k: count byte at +1,
# characters from +2) or none; some share a buffer; counts random (0, small,
# 0xfd.., frozen). message_count 20 (sometimes 1..30), message_index 0..19,
# the prompt length small. PushUiFeedback probed (verified).
MSG_BUFFERS = 0x3a000


def msg_buffer(rng, fields, k):
    at = MSG_BUFFERS + 0x100 * k
    length = rng.randrange(30)
    fields.append((at + 1, rng.choice([0, 0, 1, 2, 0xfd, 0xfe, 0xff]), 1))
    for i in range(length):
        fields.append((at + 2 + 2 * i, rng.randint(1, 0xffff), 2))
    fields.append((at + 2 + 2 * length, 0, 2))
    return WORLD_BASE + at + 2, length


def msg_string(rng, fields, at, buffers):
    if rng.random() < 0.25:
        fields += [(at + 4, 0, 4), (at + 8, 0, 4), (at + 0xc, 0, 4)]
        return
    ptr, length = rng.choice(buffers)
    fields += [(at + 4, ptr, 4), (at + 8, length, 4), (at + 0xc, length + rng.randrange(4), 4)]


def msg_state(rng, pool, args):
    fields = []
    buffers = [msg_buffer(rng, fields, k) for k in range(1 + rng.randrange(8))]
    for line in range(20):
        msg_string(rng, fields, LOGIC_THREAD + 0x414 + 0x1c * line, buffers)
    msg_string(rng, fields, LOGIC_THREAD + 0x3f8, buffers)
    fields += [(LOGIC_THREAD + 0x644, 20 if rng.random() < 0.85 else rng.randint(1, 30), 4),
               (LOGIC_THREAD + 0x648, rng.randrange(20), 4),
               (LOGIC_THREAD + 0x40c, rng.choice([0, rng.randrange(12)]), 4)]
    # The argument (ShowMessage): its fields are the stack arguments 0..3;
    # its buffer, when it has one, is one of the buffers (MSG_ARG_BUFFER).
    return fields


def msg_argument(rng, pool):
    return rng.getrandbits(32)


def msg_text_ptr(rng, pool):
    # One of the first buffers (their slots exist when msg_state made them)
    # or none.
    return rng.choice([0, WORLD_BASE + MSG_BUFFERS + 2])


def msg_text_len(rng, pool):
    return rng.randrange(30)


MSG_CALLEES = {0x0053f560: (1, "thiscall", "probe")}  # CLogicThread::PushUiFeedback


def msg_null_count(rng, pool, args):
    """The count byte 0x655dc7 before the static empty wide string 0x655dc8."""
    return [(0x00655dc7, rng.choice([0, 0, 1, 0xfd, 0xfe, 0xff]), 1)]

SPECS += [
    # Dead: the self-assignment branch 0x51e499..0x51e4f7 (the text is a
    # stack argument, never the line's own string) and 0x51e48f (a length
    # above npos). The static empty string's count byte is random
    # (msg_null_count) so that a text without buffer is also copied.
    Spec("CLogicThread_ShowMessage", 0x0051e440, "logicthread",
         [msg_argument, msg_text_ptr, msg_text_len, msg_text_len,
          lambda rng, pool: rng.choice([0, 5, 19, 20, 21, 249, 250, 251, 254, rng.getrandbits(32)])],
         eax_mask=0, presets=msg_state, callees=MSG_CALLEES, globals=msg_null_count,
         exempt_blocks=(0x0051e48f, 0x0051e499, 0x0051e4a2, 0x0051e4a7, 0x0051e4b7, 0x0051e4b9, 0x0051e4bd, 0x0051e4e4,
                        0x0051e4ec)),
    # Dead: the self-assignment branch 0x51e200 (the local is never the
    # chat); 0x51e1f6 (a length above npos); the local is a fresh copy of
    # length - 1 characters (count byte 0), so the chat always shares it
    # when it isn't empty: the null-buffer branches 0x51e224/0x51e240, the
    # copying branch 0x51e26e..0x51e27b and the local's free 0x51e2aa
    # (sharing raised its count to 1) can't run.
    Spec("CLogicThread_ChatInputKey", 0x0051e160, "logicthread",
         [lambda rng, pool: rng.choice([8, 8, 8, 13, rng.randrange(300), rng.getrandbits(32)])],
         eax_mask=0xFF, presets=msg_state,
         exempt_blocks=(0x0051e1f6, 0x0051e200, 0x0051e224, 0x0051e240, 0x0051e26e, 0x0051e276, 0x0051e27b,
                        0x0051e2aa)),
]


# MessageLine::Assign (message_lines.cpp): this = the line at world +0, the
# source at +0x40 or this itself (self-assignment); both texts from
# msg_buffer (shared or not).
def msg_line_pair(rng, pool, args):
    fields = []
    buffers = [msg_buffer(rng, fields, k) for k in range(1 + rng.randrange(4))]
    for at in (0, 0x40):
        msg_string(rng, fields, at, buffers)
    if rng.random() < 0.15:  # a source without buffer but a length (reads the static empty string)
        fields += [(0x40 + 4, 0, 4), (0x40 + 8, rng.randrange(1, 4), 4)]
    return fields


SPECS += [
    # Dead: 0x523349 (a length above npos); in the self-assignment
    # (assign(x, 0, npos) of itself) the _Xran 0x523354 and the erase of
    # the rest 0x52336f / 0x523396 (count = the length, nothing to erase).
    Spec("MessageLine_Assign", 0x00523330, "thiscall",
         [lambda rng, pool: WORLD_BASE + rng.choice([0x40, 0x40, 0x40, 0])],
         presets=msg_line_pair, globals=msg_null_count,
         exempt_blocks=(0x00523349, 0x00523354, 0x0052336f, 0x00523396)),
]


# MessageQueue_Pop / MessageQueue_Push (message_queue.cpp, wip/logic-r8):
# the queue at world offset 0 (ECX), its ring (2..64 dwords) and the
# pushed dwords in scratch slots, the event object's handle (+8) random.
# The queue's count is near the capacity often enough that Push overflows
# (the throw of a CEventQueueOverflow is compared); SetEvent fails about
# one call in four (VERIFY's ThrowWin32Error).
MQ_RING = SCRATCH + 6 * SCRATCH_SLOT_BYTES
MQ_DWORDS = SCRATCH + 7 * SCRATCH_SLOT_BYTES
MQ_EVENT = SCRATCH + 8 * SCRATCH_SLOT_BYTES


def message_queue_fields(rng, pool, args):
    capacity = 1 << rng.randrange(1, 7)
    pushed = args[1] if len(args) > 1 else 0
    if pushed >= 0x80000000:
        pushed -= 1 << 32
    roll = rng.random()
    if roll < 0.3:
        count = capacity - pushed + rng.choice([0, 1, -1])  # right at the overflow edge
    elif roll < 0.9:
        count = rng.randrange(capacity + 1)
    else:
        count = rng.getrandbits(32)
    peak = rng.choice([0, count, count + 1, count - 1, rng.randrange(capacity + 2), rng.getrandbits(32)])
    return [(0x14, WORLD_BASE + MQ_RING, 4), (0x18, capacity, 4), (0x1c, rng.randrange(capacity), 4),
            (0x20, rng.randrange(capacity), 4), (0x24, count & 0xFFFFFFFF, 4), (0x28, WORLD_BASE + MQ_EVENT, 4),
            (0x30, peak & 0xFFFFFFFF, 4)]


def message_queue_push_count(rng, pool) -> int:
    return rng.choice([0, 1, 2, 3, rng.randrange(8), rng.randrange(70), 0xFFFFFFFF, 0xFFFFFFFE])


def message_queue_signal(rng, pool) -> int:
    return rng.getrandbits(32) if rng.random() < 0.5 else rng.getrandbits(24) << 8


SPECS += [
    Spec("MessageQueue_Pop", 0x00430980, "thiscall", [], presets=message_queue_fields),
    Spec("MessageQueue_Push", 0x004309c0, "thiscall",
         [lambda rng, pool: WORLD_BASE + MQ_DWORDS, message_queue_push_count, message_queue_signal],
         eax_mask=0, presets=message_queue_fields, new_fails=0.1,
         # Dead: 0x430c58, the location's destruction after
         # ThrowWin32Error (which always throws).
         exempt_blocks=(0x00430c58,)),
]
