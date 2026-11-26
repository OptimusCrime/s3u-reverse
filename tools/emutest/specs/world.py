"""Emulator specs, part `world`: specs of the replacements in src/s3re/game/world/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def small_map_and_generation(rng, pool, args):
    return [(MAP_SIZE, rng.randint(-2, 12) & 0xFFFFFFFF, 4),
            (SEARCH_GENERATION, 0xFF if rng.random() < 0.5 else rng.randrange(256), 1)]


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


def alloc_table(table: int, stride: int, limit: int, high_water: int, size: int,
                free_value: int) -> Callable:
    """Records 1..first_free-1 in use, first_free free (often at or past the
    limit), the high-water mark near it or random."""
    def presets(rng, pool, args):
        r = rng.random()
        if r < 0.25:
            first_free = limit + 1 - rng.randrange(3)
        elif r < 0.4:
            first_free = limit + 2  # none free
        else:
            first_free = 1 + rng.randrange(300)
        fields = []
        for record in range(1, min(first_free, limit + 2)):
            value = rng.randrange(1, 1 << (8 * size))
            if value == free_value:
                value ^= 1
            fields.append((table + record * stride, value, size))
        if first_free <= limit + 1:
            fields.append((table + first_free * stride, free_value, size))
        fields.append((high_water, (first_free + rng.randint(-2, 2)) & 0xFFFFFFFF
                       if rng.random() < 0.6 else rng.getrandbits(32), 4))
        return fields
    return presets


def event_type(rng, pool):
    return 6 if rng.random() < 0.6 else anything(rng, pool)


def marked_tile(rng, pool, args):
    return [(tile(args[0], args[1]) + 0x8, rng.randrange(4), 1),
            (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]


def replace_tile(offset, size):
    """Tile field at `offset` often equal to args[2]; ring index near the end."""
    def presets(rng, pool, args):
        value = args[2] if rng.random() < 0.7 else rng.randrange(4)
        return [(tile(args[0], args[1]) + offset, value & ((1 << (8 * size)) - 1), size),
                (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]
    return presets


def marked_tile_terrain(rng, pool, args):
    return [(tile(args[0], args[1]) + 0x6, rng.choice([4, 5, 6, 7, 8, rng.randrange(256)]), 1),
            (tile(args[0], args[1]) + 0xa, rng.getrandbits(8) | (0x10 if rng.random() < 0.7 else 0), 1),
            (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]


def timers(rng, pool, args):
    return [(0x2e5e0 + entry * 12, rng.choice([0, 1, 1, 2, 3, rng.getrandbits(32)]), 4)
            for entry in range(420)]


def pile_claim_slots(rng, pool, args):
    fields = []
    for slot in range(128):
        empty = rng.random() < 0.05
        fields += [(PILE_CLAIMS + slot * 4, 0 if empty else rng.randrange(8), 2),
                   (PILE_CLAIMS + slot * 4 + 2, 0 if empty else rng.randrange(8), 2)]
    fields.append((PILE_CLAIMS + rng.randrange(128) * 4, 0, 4))  # never full
    return fields


def pile_claims_cursor(rng, pool, args):
    return [(0x7ace60, rng.choice([0, 1, 127, rng.randrange(128)]), 2)]


def players_field0c(rng, pool, args):
    fields = []
    for player in range(20):
        record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
        fields += [(record + 0x0, 0 if rng.random() < 0.25 else rng.getrandbits(32) | 1, 4),
                   (record + 0x18, rng.getrandbits(32) | 1 if rng.random() < 0.3 else 0, 4),
                   (record + 0xc, rng.randrange(6), 4)]
    return fields


PRESENCE = 0xde7448


def presence(rng, pool, args):
    """Sparse presence bytes in the 26x26 blocks of a 384 map (or smaller)."""
    n = rng.choice([384, rng.randrange(16, 384)])
    fields = [(MAP_SIZE, n, 4), (0xde7444, rng.randrange(20), 4),
              (PLAYER_RECORDS + args[3] * PLAYER_RECORD_BYTES + 0xc,
               rng.randrange(20) if rng.random() < 0.9 else rng.getrandbits(32), 4)]
    density = rng.random() * 0.05
    for bx in range(26):
        for by in range(26):
            for index in range(20):
                fields.append((PRESENCE + bx * 960 + by * 20 + index,
                               1 if rng.random() < density else 0, 1))
    return fields


def player_on_map(rng, pool, args):
    record = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    n = rng.choice([384, rng.randrange(40, 384)])
    return [(MAP_SIZE, n, 4), (record + 0x10, rng.randrange(n), 4), (record + 0x14, rng.randrange(n), 4)]

SPECS += [
    # search / statistics (world_search.cpp, statistics.cpp)
    Spec("TileDistance", 0x004ff320, "stdcall", [anything, anything, anything, anything]),
    Spec("CGameData_NextSearchGeneration", 0x004ff3f0, "thiscall", [], eax_mask=0,
         presets=small_map_and_generation),
    Spec("CGameData_InitStatisticsKernel", 0x004ff4c0, "thiscall", [], eax_mask=0),
    Spec("CGameData_ShiftStatistics", 0x004ff470, "thiscall", [], eax_mask=0,
         presets=one_active_player, max_cases=60),
    Spec("CGameData_AddStatistic", 0x004ff510, "thiscall", [below(20), below(36), anything],
         eax_mask=0),
    Spec("CGameData_SeedRandom_Impl", 0x004feb40, "thiscall", [anything], eax_mask=0),
    Spec("CGameData_Random", 0x004feb70, "thiscall", [], eax_mask=0xFFFF),
    # players (players.cpp)
    Spec("CGameData_AddPlayerCounter", 0x004ffb00, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("CGameData_SubtractPlayerCounter", 0x004ffb40, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("CGameData_ClearPlayerListLengths", 0x0050c9c0, "thiscall", []),
    # counting (counting.cpp)
    Spec("CGameData_CountBuildings", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountBuildingsWithFlag21Clear", 0x005158a0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountBuildingsWithWord8", 0x005159d0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountSettlers", 0x00515a30, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_CountSettlersWithJob", 0x00515a80, "thiscall", [below(5), below(5)],
         presets=record_counts),
    # free ids (id_allocators.cpp)
    Spec("CGameData_FindFreeId256", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("CGameData_FindFreeId4096", 0x0051d570, "thiscall", [],
         presets=id_table(0xe115e0, 0xe115e4, 0x1000), max_cases=400),
    Spec("CGameData_FindFreeId8192", 0x0051c040, "thiscall", [],
         presets=id_table(0xe125e4, 0xe125e8, 0x2000), max_cases=200),
    # tables, rings, timers (world_tables.cpp)
    Spec("CGameData_AllocBuildingExtra", 0x004df0a0, "thiscall", [anything],
         presets=alloc_table(0x10f45e8, 0x5a, 998, 0x110a578, 1, 0xff), max_cases=600),
    Spec("CGameData_AllocTable36700Record", 0x004fefd0, "thiscall", [anything] * 10,
         presets=alloc_table(0x36700, 20, 9998, 0x67440, 2, 0), max_cases=150),
    Spec("CGameData_QueueEvent", 0x004e8ad0, "thiscall",
         [event_type, below(4, 0.1), anything, anything], presets=event_rings),
    Spec("CGameData_QueueMarkedTile", 0x004e6c40, "thiscall",
         [below(384), below(384), below(4, 0.1)], eax_mask=0, presets=marked_tile),
    Spec("CGameData_QueueMarkedTileTerrain5To7", 0x0051ce50, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("CGameData_QueueMarkedTileNoOwner", 0x0051d3d0, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("CGameData_ReplaceTileWord16AndQueue", 0x0051c560, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x16, 2)),
    Spec("CGameData_ReplaceTileByte10AndQueue", 0x0051c820, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x10, 1)),
    Spec("CGameData_CountDownTimers", 0x004fee00, "thiscall", [], eax_mask=0, presets=timers,
         max_cases=200),
    Spec("CGameData_AddPileClaim", 0x00510770, "thiscall", [below(8, 0.1), below(8, 0.1)],
         eax_mask=0, presets=pile_claim_slots, globals=pile_claims_cursor, max_cases=800),
    # player queries (player_queries.cpp)
    Spec("CGameData_FindPlayerByField0C", 0x00507f10, "thiscall", [below(6, 0.1)],
         presets=players_field0c),
    Spec("BuildingTypeAllowedForRace", 0x004ffd60, "stdcall", [anything, building_type],
         eax_mask=0xff),
    Spec("CGameData_RandomBelow6", 0x00503880, "thiscall", []),
    Spec("CGameData_IsOtherPlayerNear", 0x00510650, "thiscall",
         [below(400), below(400), below(40), below(20)], presets=presence, max_cases=400),
    Spec("CGameData_LogPlayerLandResources", 0x00507f60, "thiscall", [below(20)], presets=player_on_map,
         max_cases=6),
]


def pending_type(rng, pool):
    return rng.choice([4, 4, 4, 6, rng.randrange(10), anything(rng, pool)])


def pending_rings(rng, pool, args):
    """The 12-byte ring: read/write indices (sometimes the write index at the
    end), entries in between often {4, 0..3}."""
    read = rng.choice([rng.randrange(1000), 999, 995])
    length = rng.choice([0, 1, rng.randrange(12)])
    write = (read + length) % 1000
    fields = []
    for i in range(length):
        entry = PENDING_RING + ((read + i) % 1000) * 12
        fields += [(entry, 4 if rng.random() < 0.5 else rng.randrange(10), 4), (entry + 4, rng.randrange(4), 4)]
    if rng.random() < 0.1:
        write = 999
    return fields + [(0x32870, read, 4), (0x32874, write, 4)]

SPECS += [
    Spec("CGameData_QueuePendingEvent", 0x004e8ba0, "thiscall", [pending_type, below(4, 0.1), anything],
         presets=pending_rings),
]


AREA_SPAN = 12  # settlers stand on tiles 0..11 x 0..11


def area_settlers(rng, pool, args):
    """Up to 40 settlers of players 0..3 with types 0..0x2f (sometimes any)
    near (0, 0), most on their own tile; tile areas and the area's player
    0..3 (sometimes free); count 0..40."""
    count = rng.choice([0, 1, rng.randrange(41), rng.randrange(41)])
    area = args[0]
    fields = [(SETTLER_COUNT, count, 4),
              (AREAS + area * AREA_BYTES, 0xff if rng.random() < 0.125 else rng.randrange(4), 1)]
    for x in range(AREA_SPAN):
        for y in range(AREA_SPAN):
            fields.append((tile_area_offset(x, y), rng.getrandbits(16) if rng.random() < 1 / 16 else rng.randrange(4), 2))
            fields.append((tile(x, y), 0, 2))
    for s in range(1, count + 1):
        record = SETTLERS + s * SETTLER_BYTES
        x, y = rng.randrange(AREA_SPAN), rng.randrange(AREA_SPAN)
        fields += [(record, rng.randrange(4), 1),
                   (record + 1, rng.randrange(256) if rng.random() < 0.1 else rng.randrange(0x30), 1),
                   (record + 4, x, 2), (record + 6, y, 2)]
        if rng.random() < 2 / 3:
            fields.append((tile(x, y), s, 2))
    return fields


def area_settler_type(rng, pool):
    return rng.randrange(0x30) | (0x100 if rng.random() < 0.125 else 0)

SPECS += [
    Spec("CGameData_CountAreaSettlersOfClass0", 0x0050e750, "thiscall", [below(4)], presets=area_settlers,
         max_cases=500),
    Spec("CGameData_CountAreaSettlersOfType", 0x0050e820, "thiscall", [below(4), area_settler_type],
         presets=area_settlers, max_cases=500),
]


NOTIFICATION_LISTS = 0x2d320
NOTIFICATION_TIMERS = 0x2e5e0
CONFIG_NOTIFICATION_LEVEL = 0x7a7af0 + 0x64
UI_FEEDBACK_COUNT = 0x15394ac


def notification_state(rng, pool, args):
    """Local player mostly the caller; the type's timers: a few running near
    (x, y), the rest free (sometimes all running); the lists mostly with
    room (sometimes full); the UI feedback list sometimes full."""
    player, kind, x, y = args
    fields = [(LOCAL_PLAYER, player if rng.random() < 0.85 else rng.randrange(4), 4),
              (UI_FEEDBACK_COUNT, rng.randrange(11), 4)]
    running = 10 if rng.random() < 0.1 else rng.randrange(4)
    for k in range(10):
        entry = NOTIFICATION_TIMERS + (kind * 10 + k) * 12
        if k < running:
            fields += [(entry, rng.randrange(1, 100), 4),
                       (entry + 4, (x + rng.randint(-60, 60)) & 0xFFFFFFFF, 4),
                       (entry + 8, (y + rng.randint(-60, 60)) & 0xFFFFFFFF, 4)]
        else:
            fields.append((entry, 0 if rng.random() < 0.8 else rng.randrange(1, 100), 4))
    used = 100 if rng.random() < 0.1 else rng.randrange(6)
    for lst in range(-1, 4):
        for k in range(100):
            entry = NOTIFICATION_LISTS + (lst * 100 + k) * 12
            fields.append((entry & 0xFFFFFFFF, rng.randrange(1, 40) if k < used else 0, 4))
    return fields


def notification_level(rng, pool, args):
    return [(CONFIG_NOTIFICATION_LEVEL, rng.choice([0, 1, 3, 5, 7, 8, 9, 10, rng.getrandbits(32)]), 4)]

SPECS += [
    Spec("CGameData_NotifyLocalPlayer", 0x004fec10, "thiscall",
         [below(4), below(42), below(384), below(384)], eax_mask=0,
         presets=notification_state, globals=notification_level, max_cases=1500),
]


# -- session 54 (logic-misc): FinishInitialising (finish_initialising.cpp), BuildStatisticsChart
#    (statistics.cpp), GoToNotification (notifications.cpp) --

def finish_initialising_world(rng, pool, args):
    return [(0x1195dcc, 0 if rng.random() < 0.5 else rng.getrandbits(8), 1),
            (MAP_SIZE, rng.choice([64, 128, 384, rng.randrange(800), rng.getrandbits(32)]), 4)]


STATISTICS_KERNEL = 0x4
STATISTICS_HISTORIES = 0x38
GAME_KERNEL = (3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3)


def statistics_chart_world(rng, pool, args):
    """The game's kernel or small random weights (high bits only where they
    don't reach the divisor), and a history: sparse, dense, small or random."""
    player, category = args
    fields = []
    for k in range(13):
        w = GAME_KERNEL[k] if rng.random() < 0.6 else rng.randrange(16)
        if k >= 6 and rng.random() < 0.2:
            w |= rng.getrandbits(16) << 16
        fields.append((STATISTICS_KERNEL + 4 * k, w, 4))
    kind = rng.randrange(5)
    history = STATISTICS_HISTORIES + (player * 36 + category) * 256
    for i in range(256):
        if kind == 4:  # (almost) empty: the chart fits the first axis scale
            v = 1 if rng.random() < 0.01 else 0
        elif kind == 0:
            v = rng.getrandbits(8)
        elif kind == 1:
            v = rng.randrange(101) if rng.random() < 0.3 else 0
        else:
            v = rng.randrange(101 if kind == 2 else 8)
        fields.append((history + i, v, 1))
    return fields


GO_TO_TICK = [5000]  # (GAME_TICK: logic.py)


def go_to_notification_world(rng, pool, args):
    """The list's entries (a few, sometimes full), a stored return position
    or none, the last jump recent or not, a map size; tick in the globals."""
    GO_TO_TICK[0] = 1000 + rng.randrange(100000)  # (globals run after the presets)
    row = NOTIFICATION_LISTS + (args[0] - 1) * 1200
    size = rng.randrange(64, 385)
    used = 100 if rng.random() < 0.15 else rng.randrange(1, 12)
    fields = [(MAP_SIZE, size, 4), (0x2d314, 0 if rng.random() < 0.25 else rng.randrange(384), 4),
              (0x2d31c, GO_TO_TICK[0] - rng.randrange(400) if rng.random() < 0.6 else rng.getrandbits(32), 4)]
    for k in range(100):
        t = rng.randrange(1, 42) if k < used else (7 if rng.random() < 0.3 else 0)
        fields += [(row + k * 12, t, 4), (row + k * 12 + 4, rng.randrange(2, size - 2), 4),
                   (row + k * 12 + 8, rng.randrange(2, size - 2), 4)]
    return fields


def go_to_notification_globals(rng, pool, args):
    return [(GAME_TICK, GO_TO_TICK[0], 4)] + screen_size(rng, pool, args)


SPECS += [
    # EAX: DebugLog's (callers ignore it).
    Spec("CGameData_FinishInitialising", 0x004fce90, "thiscall", [], eax_mask=0, presets=finish_initialising_world,
         max_cases=300),
    Spec("CGameData_BuildStatisticsChart", 0x004ff550, "thiscall", [below(20), below(36)], eax_mask=0,
         presets=statistics_chart_world, max_cases=300),
    # The marker effect is probed (MapEffects::Spawn_5450d0 has its own spec);
    # the camera and screen-position helpers run.
    Spec("CGameData_GoToNotification", 0x004fee90, "thiscall", [between(1, 4)], eax_mask=0,
         presets=go_to_notification_world, globals=go_to_notification_globals, max_cases=500,
         callees={0x005450d0: (5, "thiscall", "probe")}),
]


# -- CGameData::BuildOffsetTable (offset_table.cpp, session 57): no inputs;
# 3333 searches over 5000 points (~50M instructions), so a few cases.
SPECS += [
    Spec("CGameData_BuildOffsetTable", 0x00516530, "thiscall", [], eax_mask=0, max_cases=3, quick_cases=1,
         max_instructions=200_000_000),
]


# -- CGameData destructors (world_object.cpp) and InitAfterMapLoad
# (init_after_map_load.cpp), session 57. InitAfterMapLoad's callees are
# probed (verified on their own, or the original InitPlayerInfo); the
# players' active flags and races (0..4, 4 reads past the spell table).
def init_after_map_load_players(rng, pool, args):
    fields = []
    for p in range(20):
        base = 0x11943dc + 0x148 * p
        fields += [(base, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4), (base + 4, rng.randrange(5), 4)]
    return fields


SPECS += [
    Spec("CGameData_Destroy", 0x004fce80, "thiscall", [], eax_mask=0, cover_until=0x004fce8b),
    Spec("CGameData_DeletingDestroy", 0x004fce60, "thiscall", [below(4)]),
    Spec("CGameData_InitAfterMapLoad", 0x004fd020, "thiscall", [], eax_mask=0, presets=init_after_map_load_players,
         callees={address: (0, "thiscall", "probe") for address in
                  (0x00519e80, 0x00518e70, 0x004fb650, 0x0050c9c0, 0x004fd0d0, 0x0051b6b0)}),
]


# -- CGameData::InitPlayerInfo (player_info.cpp; spec wip/logic-r4) ----------
# The settings' player setups are fake records in the world (PlayerSetup
# 0x4351e0 probed: setup p at IPI_SETUPS + 0x100 * p; its type +0x5c read
# by the original getter: 0..3 or any); IsMultiplayer probed (AL random);
# the lobby name 0x5db260 (network code) probed: it builds a string in its
# out argument (a buffer in the world, refcount 0, 0..20 characters) and
# returns it (the out argument, a frame address, not recorded); the text
# lookup probed: null or a text in the world; LogPlayerLandResources
# probed (verified on its own). The std::string code runs; the names in
# g_playerInfo start from the image (empty).
IPI_SETUPS = 0x36000
IPI_LOBBY_TEXT = 0x38000
IPI_TEXT = 0x39000


def ipi_setup(machine, ecx, args, h):
    return WORLD_BASE + IPI_SETUPS + 0x100 * (args[0] & 0x1f)


def ipi_lobby_name(machine, ecx, args, h):
    out = args[0]
    slot = len(machine.probe_calls) & 0x1f
    text = WORLD_BASE + IPI_LOBBY_TEXT + 0x40 * slot + 1
    length = (h >> 8) % 21
    chars = bytes(0x41 + (h >> (12 + i)) % 26 for i in range(length))
    machine.guest_write(text - 1, b"\0" + chars + b"\0")
    machine.guest_write(out, struct.pack("<IIII", (h >> 40) & 0xFF, text if length else 0, length, length))
    return out


def ipi_text(h):
    return 0 if h % 4 == 0 else WORLD_BASE + IPI_TEXT + 0x40 * ((h >> 4) % 4)


def ipi_state(rng, pool, args):
    fields = []
    for p in range(20):
        fields.append((IPI_SETUPS + 0x100 * p + 0x5c, rng.choice([0, 1, 1, 2, 2, 3, 3, rng.getrandbits(32)]), 4))
        fields.append((0x11943dc + 0x148 * p, rng.choice([0, rng.getrandbits(32)]), 4))
    for k in range(4):
        length = rng.randrange(30)
        fields += [(IPI_TEXT + 0x40 * k + i, rng.randrange(0x20, 0x7f), 1) for i in range(length)]
        fields.append((IPI_TEXT + 0x40 * k + length, 0, 1))
    return fields


SPECS += [
    # Dead blocks: 0x4fd17e (the first "" assignment's null check of the
    # static "" 0x7a7868, a constant); 0x4fd38c/0x4fd390 (the inlined _Tidy
    # of the "(" temporary decrementing a shared buffer: the temporary is a
    # fresh concatenation result, refcount 0); 0x4fd483 (copying the chars
    # of "" after _Grow(0), which returns false for 0).
    Spec("CGameData_InitPlayerInfo", 0x004fd0d0, "thiscall", [], eax_mask=0, presets=ipi_state, max_cases=300,
         exempt_blocks=(0x4fd17e, 0x4fd38c, 0x4fd390, 0x4fd483),
         callees={0x004351e0: (1, "thiscall", "probe", ipi_setup),     # GameSettings::PlayerSetup
                  0x004318d0: (0, "thiscall", "probe"),                # GameSettings::IsMultiplayer
                  0x005db260: (1, "thiscall", "probe", ipi_lobby_name, lambda machine, ecx, args: ()),
                  0x005faaa0: (2, "thiscall", ipi_text),               # CGraphicsData::TextAddress
                  0x00507f60: (1, "thiscall", "probe")}),              # LogPlayerLandResources
]
