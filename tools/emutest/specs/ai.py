"""Emulator specs, part `ai`: specs of the replacements in src/s3re/game/ai/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""

# -- batch 23: computer-player helpers (ai_helpers.cpp) ---------------------

AI_WORLD = 0x007e3408
AI_PLAYER_DATA = 0x007e2eb0
AI_AREAS = 0xe145e8
AI_PILES = 0x123885c


def ai_world(rng, pool, args):
    """g_aiWorld points at the emulated world."""
    return [(AI_WORLD, WORLD_BASE, 4)]


def ai_player_data(rng, pool, args):
    """g_aiPlayerData points at scratch buffer 0 (its candidate records)."""
    return [(AI_PLAYER_DATA, WORLD_BASE + SCRATCH, 4)]


def word_list(rng, pool, args):
    """A word list at scratch 0: free slots (0xffff) and small values, so
    the searched value is often present."""
    return [(SCRATCH + 2 * i, 0xFFFF if rng.random() < 0.3 else rng.randrange(6), 2)
            for i in range(16)]


def ai_map_size(rng, pool, args):
    return [(MAP_SIZE, rng.randint(-3, 400) & 0xFFFFFFFF, 4)]


def clamp_points(rng, pool, args):
    fields = ai_map_size(rng, pool, args)
    for slot in (0, 1):
        value = rng.randint(-20, 420) if rng.random() < 0.8 else rng.getrandbits(32)
        fields.append((SCRATCH + slot * SCRATCH_SLOT_BYTES, value & 0xFFFFFFFF, 4))
    return fields


def area_pile_list(rng, pool, args):
    """Goods type (byte, scratch 0) and area (word < 4096, scratch 1), and
    a list of up to 7 distinct piles for them."""
    goods = rng.randrange(256)
    area = rng.randrange(4096)
    fields = [(SCRATCH, goods, 1), (SCRATCH + SCRATCH_SLOT_BYTES, area, 2)]
    start, stride = rng.randint(1, 20000), rng.randint(1, 1000)
    head = 0
    for k in range(rng.randrange(8)):
        pile = start + k * stride
        fields.append((AI_PILES + pile * 14 + 0xc, head, 2))
        head = pile
    fields.append((AI_AREAS + area * 0x2e0 + 0xa + goods * 2, head, 2))
    return fields


def building_lookup(rng, pool, args):
    fields = [(BUILDING_COUNT, rng.randrange(3000), 4)]
    if rng.random() < 0.3:
        fields.append((BUILDINGS + (args[0] & 0xFFFF) * BUILDING_BYTES, 0xFF, 1))
    return fields


def best_candidate(rng, pool, args):
    """Often a stored best score next to the new one (args[1])."""
    index = args[0] & 0xFFFF
    if rng.random() < 0.6:
        return [(SCRATCH + 0x26 + index * 6, (args[1] + rng.randint(-2, 2)) & 0xFFFF, 2)]
    return []

SPECS += [
    Spec("AI_SetCurrentPlayer", 0x00551a30, "fastcall", [scratch(0)]),
    Spec("AI_WordListInsert", 0x00551970, "stdcall", [id_with_noise(8), scratch(0), id_with_noise(17)],
         presets=word_list),
    Spec("AI_WordListRemove", 0x005519b0, "stdcall", [id_with_noise(8), scratch(0), id_with_noise(17)],
         presets=word_list),
    Spec("AI_IsOnMap", 0x00572090, "fastcall", [between(-5, 410)], eax_mask=0xFF,
         presets=ai_map_size, globals=ai_world),
    Spec("AI_ClampToMap", 0x00572050, "fastcall", [scratch(0), scratch(1)], presets=clamp_points,
         globals=ai_world),
    Spec("AI_AreaGoodsAmount", 0x0059dc10, "fastcall", [scratch(0), scratch(1)],
         presets=area_pile_list, globals=ai_world),
    Spec("AI_GetBuilding", 0x0058e170, "fastcall", [id_with_noise(3200), scratch(1)],
         presets=building_lookup, globals=ai_world),
    Spec("AI_RecordBestCandidate", 0x0058ec50, "fastcall",
         [id_with_noise(0x300), anything, anything, anything], presets=best_candidate,
         globals=ai_player_data),
    Spec("AI_CEvaLand_GridCell", 0x00554700, "thiscall", [anything, anything]),
    Spec("AI_CEvaLand_FreeGrid", 0x00554040, "thiscall", [], eax_mask=0,
         presets=lambda rng, pool, args: [(0x158, 0, 4)] if rng.random() < 0.3 else []),
    Spec("AI_CopySixBytes", 0x0055a420, "fastcall", [one_of(0, WORLD_BASE + SCRATCH), scratch(1)],
         eax_mask=0),
    Spec("AI_SetWord4", 0x0055ca60, "thiscall", [anything], eax_mask=0xFFFF),
]

# -- batch 24: CStaticControl, CSurface, AI area lookups, CreatePile, -------
# -- UpdateGroupMenu -------------------------------------------------------

AI_MAP_SIZES = 0x007e3412  # uint16 y size, then uint16 x size at 0x7e3414
AI_MAP_WORLD = 0x007e7b60


def building_position(rng, pool, args):
    """The building record of the id in scratch 0: x/y around the map size."""
    building = rng.randrange(3200)
    record = BUILDINGS + building * BUILDING_BYTES
    coordinate = lambda: rng.randrange(420) if rng.random() < 0.9 else rng.getrandbits(16)  # noqa: E731
    return [(SCRATCH, building, 2), (record + 0xa, coordinate(), 2), (record + 0xc, coordinate(), 2)]


def ai_map_sizes(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randrange(420), 2), (AI_MAP_SIZES + 2, rng.randrange(420), 2)]


def free_tile_world(rng, pool, args):
    """A small map; a run of blocked tiles from (x, y) on, often to the edge."""
    size = rng.randint(1, 40)
    fields = [(MAP_SIZE, size, 4)]
    x, y = args
    if 0 <= x < size and 0 <= y < size:
        run = 0 if rng.random() < 0.3 else rng.randint(1, size - x + 1)
        for k in range(run + 1):
            if x + k > size:
                break
            tile = 0x67444 + ((x + k) * 0x300 + y) * 24
            flags = rng.getrandbits(8)
            fields.append((tile + 0xa, flags | 1 if k < run else flags & ~1, 1))
    return fields


def ai_both_worlds(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_WORLD, WORLD_BASE, 4)]

SPECS += [
    Spec("AI_BuildingArea", 0x00599610, "fastcall", [scratch(0)], presets=building_position,
         globals=ai_map_sizes),
    Spec("AI_FreeTileArea", 0x0058b480, "stdcall", [between(-3, 42), between(-3, 42)],
         presets=free_tile_world, globals=ai_both_worlds),
]

# -- batch 25: computer-player helpers (ai_helpers.cpp) ---------------------


def count_buildings_world(rng, pool, args):
    count = rng.randrange(30)
    fields = [(BUILDING_COUNT, count, 4)]
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x = rng.randrange(400) if rng.random() < 0.9 else 500
        y = rng.randrange(400)
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1), (record + 6, rng.randrange(3), 1),
                   (record + 0x21, int(rng.random() < 0.25), 1), (record + 0xa, x, 2), (record + 0xc, y, 2)]
        if x < 400:
            fields.append((TILES + (x * 0x300 + y) * 24 + 0x14, rng.randrange(4), 2))
    return fields


def ai_world_400(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, 400, 2), (AI_MAP_SIZES + 2, 400, 2)]


def area_row_world(rng, pool, args):
    x, y = args[0] & 0xFFFF, args[1] & 0xFFFF
    fields = []
    for k in range(20):
        if x + k < 0x300 and y < 0x300:
            value = 0 if rng.random() < 0.8 else rng.randrange(1, 0x10000)
            fields.append((TILES + ((x + k) * 0x300 + y) * 24 + 0x14, value, 2))
    return fields


def ai_small_map(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randrange(1, 60), 2), (AI_MAP_SIZES + 2, rng.randrange(1, 60), 2)]


def list32_world(rng, pool, args):
    return [(0x200 + 2 * i, 0xFFFF if rng.random() < 0.1 else rng.randrange(6), 2) for i in range(50)]


GRID_CELLS = SCRATCH + 2 * SCRATCH_SLOT_BYTES


def grid_world(rng, pool, args):
    return [(0x140, rng.randint(1, 6), 4), (0x144, rng.randint(1, 6), 4), (0x158, WORLD_BASE + GRID_CELLS, 4)]


def small_with_noise(low: int, high: int) -> Generator:
    def generate(rng, pool):
        value = rng.randint(low, high) & 0xFFFF
        return value | (rng.getrandbits(16) << 16) if rng.random() < 0.3 else value
    return generate


def ai_vectors(rng, pool, args):
    """The 20 vectors (begin at -4, end at +0 of each 16-byte entry)."""
    return [(0x007e7a24 + 4 * i, rng.getrandbits(32), 4) for i in range(0x50)]

SPECS += [
    Spec("AI_CountBuildings", 0x0059dac0, "fastcall", [below(3), below(3), one_of(0, 0, 1, 2, 3, 0x10000)],
         presets=count_buildings_world, globals=ai_world_400, max_cases=600),
    Spec("AI_AreaAlongRow", 0x005996c0, "fastcall", [small_with_noise(0, 64), small_with_noise(0, 64)],
         eax_mask=0xFFFF, presets=area_row_world, globals=ai_small_map),
    Spec("CComputerPlayer_DoomNote", 0x00551910, "thiscall", [id_with_noise(8)], presets=list32_world),
    Spec("AI_CEvaLand_GridCellWrapped", 0x00554730, "thiscall", [anything, anything]),
    Spec("AI_CEvaLand_GridSumAround", 0x00554530, "thiscall", [small_with_noise(-1, 6), small_with_noise(-1, 6)],
         presets=grid_world),
    # DEAD 0x55b802: the element copy of an inlined erase(begin, end) whose
    # source range is empty (it starts with source = end).
    Spec("AI_ResetAgentData", 0x0055b770, "thiscall", [], globals=ai_vectors, max_cases=200,
         exempt_blocks=(0x55b802,)),
    Spec("AI_CEvaLand_ResetGlobals", 0x00553e00, "stdcall", [],
         globals=lambda rng, pool, args: [(0x007e0568, rng.getrandbits(32), 4), (0x007e056c, rng.getrandbits(32), 4)]),
    Spec("AI_SiteTree_begin", 0x00559260, "thiscall", [scratch(0)],
         presets=lambda rng, pool, args: [(4, WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES, 4)]),
]

# -- batch 26: computer player helpers (ai_small_helpers.cpp, ui_state.cpp) --


def word8_record(rng, pool, args):
    return list32_world(rng, pool, args) + [(SCRATCH + 8, rng.getrandbits(16), 2)]


def tile_words(rng, pool, args):
    return [(SCRATCH, rng.randrange(70), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(70), 2)]


def building_count(rng, pool, args):
    count = rng.randrange(40)
    fields = [(0x1499e60, count, 4)]
    id_ = args[0] & 0xFFFF
    if id_ < 64 and rng.random() < 0.4:
        fields.append((0x1499e64 + id_ * 0x3a, 0xFF, 1))
    return fields

SPECS += [
    Spec("CComputerPlayer_DoomNoteId", 0x00551780, "thiscall", [anything], presets=list32_world),
    Spec("CComputerPlayer_DoomNoteOf", 0x00551760, "thiscall", [scratch(0)], presets=word8_record),
    Spec("AI_CEvaLand_ResetGrid", 0x00554070, "thiscall", []),
    Spec("AI_TileCentre_554880", 0x00554880, "stdcall", [anything, anything, scratch(0), scratch(1)]),
    # Callers use AX; off the map the upper half is the caller's EAX.
    Spec("AI_TileOwner", 0x00599570, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=tile_words, globals=ai_small_map),
    Spec("AI_CellCentreOwner", 0x00554850, "stdcall",
         [small_with_noise(0, 9), small_with_noise(0, 9)], globals=ai_small_map),
    Spec("AI_CEvaLand_GridCellOfTile", 0x00554770, "thiscall", [anything, anything], presets=grid_world),
    Spec("AI_BuildingIfUsed_58e1c0", 0x0058e1c0, "fastcall", [small_with_noise(0, 64)],
         presets=building_count, globals=ai_world),
]


# -- computer players, batch 1 (session 47): CComputerPlayer, BLACK_BOARD_AGENT,
# the census (ai_computer_player.cpp, ai_census.cpp) -------------------------

AI_PLAYER = 0x007E3130  # g_aiPlayer
AGENT_POOL = SCRATCH  # ten agents of 0x20 bytes (ids 0..9)
AGENT_POOL_SIZE = 10
AI_DATA_NODE = SCRATCH + SCRATCH_SLOT_BYTES  # an AiPlayerData node
AI_SECOND_PLAYER = SCRATCH + 2 * SCRATCH_SLOT_BYTES  # g_aiPlayer for FindChildOfType


def agent_pool(rng, table):
    """Agents 0..9 in the pool, their pointers in the agent table at
    `table` (world offset); children chains only go to higher ids."""
    fields = []
    for agent_id in range(AGENT_POOL_SIZE):
        agent = AGENT_POOL + agent_id * 0x20
        fields.append((table + 4 * agent_id, WORLD_BASE + agent, 4))
        later = AGENT_POOL_SIZE - agent_id - 1
        sibling = 0xFFFF if later == 0 or rng.random() < 0.25 else agent_id + 1 + rng.randrange(later)
        fields += [(agent + 4, rng.randrange(8) if rng.random() < 0.6 else rng.getrandbits(16), 2),
                   (agent + 6, rng.randrange(4) if rng.random() < 0.8 else rng.getrandbits(16), 2),
                   (agent + 8, agent_id if rng.random() < 0.5 else rng.randrange(AGENT_POOL_SIZE), 2),
                   (agent + 0x12, sibling, 2),
                   (agent + 0x14, 0xFFFF if rng.random() < 0.25 else rng.randrange(AGENT_POOL_SIZE), 2)]
    return fields


def computer_player(rng, pool, args):
    """A CComputerPlayer at the world base: its four id lists (small ids,
    a free tail, sometimes full), agents 0..9, an AiPlayerData node."""
    fields = []
    for offset, slots in ((0x4, 100), (0xCE, 50), (0x134, 100), (0x200, 50)):
        used = slots if rng.random() < 0.15 else rng.randrange(slots)
        fields += [(offset + 2 * k, rng.randrange(12) if k < used else 0xFFFF, 2) for k in range(slots)]
    fields += agent_pool(rng, 0x280)
    fields.append((0x778, WORLD_BASE + AI_DATA_NODE, 4))
    for building_type in range(57):
        r = rng.random()
        priority = 0 if r < 0.3 else rng.randrange(1000) if r < 0.5 else rng.getrandbits(16)
        fields.append((AI_DATA_NODE + 0x26 + 6 * building_type, priority, 2))
    return fields


def pool_agent(rng, pool):
    return WORLD_BASE + AGENT_POOL + rng.randrange(AGENT_POOL_SIZE) * 0x20


def agent_with_children(rng, pool, args):
    """The agent at the world base and its children in the pool of the
    player g_aiPlayer points at."""
    return agent_pool(rng, AI_SECOND_PLAYER + 0x280) + [
        (0x14, 0xFFFF if rng.random() < 0.2 else rng.randrange(AGENT_POOL_SIZE), 2)]


def ai_second_player(rng, pool, args):
    return [(AI_PLAYER, WORLD_BASE + AI_SECOND_PLAYER, 4)]


def flag_bits(rng, pool):
    return 1 << rng.randrange(4) if rng.random() < 0.6 else rng.getrandbits(32)


AI_BUILDINGS = 0x1499E64
AI_PLAYERS = 0x11943DC


def ai_map_world(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_WORLD, WORLD_BASE, 4)]


def census_buildings(rng, pool, args):
    """Up to 40 buildings: owners mostly < 20, types mostly < 57, mostly
    finished; sometimes no buildings or a count of 0xffffffff."""
    r = rng.random()
    count = 0 if r < 0.05 else 0xFFFFFFFF if r < 0.08 else rng.randrange(40)
    fields = [(0x1499E60, count, 4)]
    for building_id in range(1, 41):
        b = AI_BUILDINGS + building_id * 0x3A
        fields += [(b, rng.randrange(20) if rng.random() < 0.85 else rng.getrandbits(8), 1),
                   (b + 6, rng.randrange(57) if rng.random() < 0.9 else rng.getrandbits(8), 1),
                   (b + 0x21, 0 if rng.random() < 0.75 else rng.getrandbits(8), 1)]
    return fields


def census_players(rng, pool, args):
    """Players taking part or not, in a few teams; small census counts."""
    fields = []
    for p in range(20):
        record = AI_PLAYERS + p * 0x148
        fields += [(record, 0 if rng.random() < 0.3 else rng.randrange(1, 3), 4),
                   (record + 0xC, rng.randrange(4), 4)]
        fields += [(0x444 + (p * 9 + k) * 4, rng.randrange(50), 4) for k in range(9)]
        fields += [(0x714 + (p * 57 + t) * 4, rng.randrange(10), 4) for t in (15, 16, 18)]
    return fields


def start_position(rng, pool, args):
    """A small map; the player's start position (sometimes off the map)
    with a run of blocked tiles to its right, often to the edge."""
    size = rng.randint(1, 40)
    record = AI_PLAYERS + args[0] * 0x148
    def coordinate():
        return rng.randint(-3, size + 2) & 0xFFFFFFFF if rng.random() < 0.15 else rng.randrange(size)
    x, y = coordinate(), coordinate()
    fields = [(MAP_SIZE, size, 4), (record + 0x10, x, 4), (record + 0x14, y, 4)]
    if x < size and y < size:
        run = rng.randint(0, size - x + 1)
        for k in range(run + 1):
            if x + k >= size:
                break
            t = TILES + ((x + k) * 0x300 + y) * 24
            flags = rng.getrandbits(8)
            fields += [(t + 0xA, flags | 1 if k < run else flags & ~1, 1), (t + 0x10, rng.getrandbits(8), 1)]
    return fields


def ptr_vector(rng, pool, args):
    first = 0 if rng.random() < 0.2 else WORLD_BASE + SCRATCH + 4 * rng.randrange(16)
    last = first + 4 * rng.randrange(20) if rng.random() < 0.8 else rng.getrandbits(32)
    return [(4, first, 4), (8, last & 0xFFFFFFFF, 4)]


class CopyRange:
    """Arguments of AI_UninitializedCopyPtrs: (first, last, dest). A null
    destination only comes with a range of at most one element (the
    original then writes to address 4 onwards)."""

    def __init__(self):
        self.count = 0

    def first(self, rng, pool):
        return WORLD_BASE + SCRATCH

    def last(self, rng, pool):
        self.count = rng.randrange(20)
        return WORLD_BASE + SCRATCH + 4 * self.count

    def dest(self, rng, pool):
        if self.count <= 1 and rng.random() < 0.5:
            return 0
        return WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 4 * rng.randrange(4)


COPY_RANGE = CopyRange()


def grid_world_or_empty(rng, pool, args):
    return [(0x140, rng.randint(0, 6), 4), (0x144, rng.randint(0, 6), 4), (0x158, WORLD_BASE + GRID_CELLS, 4)]

SPECS += [
    Spec("CComputerPlayer_AddRootNote", 0x005517f0, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemoveRootNote", 0x00551820, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_AddPendingNote", 0x00551850, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemovePendingNote", 0x00551880, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_AddActiveNote", 0x005518b0, "thiscall", [id_with_noise(12)], presets=computer_player),
    Spec("CComputerPlayer_RemoveActiveNote", 0x005518e0, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_RemoveDoomedNote", 0x00551940, "thiscall", [id_with_noise(12)],
         presets=computer_player),
    Spec("CComputerPlayer_ActivateNote", 0x005515e0, "thiscall", [id_with_noise(AGENT_POOL_SIZE)],
         presets=computer_player),
    # EDX's upper half goes into the pushed id (mov dx).
    Spec("CComputerPlayer_ActivateNoteOf", 0x005515c0, "thiscall", [pool_agent], presets=computer_player),
    Spec("CComputerPlayer_DeactivateNote", 0x00551630, "thiscall", [id_with_noise(AGENT_POOL_SIZE)],
         presets=computer_player),
    Spec("CComputerPlayer_DeactivateNoteOf", 0x00551610, "thiscall", [pool_agent], presets=computer_player),
    Spec("CComputerPlayer_RaiseBuildPriorities", 0x00551b10, "thiscall", [], eax_mask=0,
         presets=computer_player),
    Spec("BLACK_BOARD_NOTE_ResetLinks", 0x00582400, "thiscall", []),
    Spec("BLACK_BOARD_NOTE_Init", 0x005823e0, "thiscall", [anything]),
    # AX only: the upper half of EAX is the caller's.
    Spec("BLACK_BOARD_NOTE_MarkDispatched", 0x00582430, "thiscall", [], eax_mask=0xFFFF),
    Spec("BLACK_BOARD_NOTE_SetFlags", 0x00582440, "thiscall", [flag_bits], eax_mask=0xFFFF),
    Spec("BLACK_BOARD_NOTE_ClearFlags", 0x00582450, "thiscall", [flag_bits]),
    Spec("BLACK_BOARD_NOTE_HasFlags", 0x00582460, "thiscall", [flag_bits]),
    Spec("BLACK_BOARD_NOTE_FindChildOfType", 0x005824d0, "thiscall", [id_with_noise(5)],
         presets=agent_with_children, globals=ai_second_player),
    Spec("AI_Census_Construct", 0x0055b6b0, "thiscall", [], globals=ai_vectors, max_cases=200),
    Spec("AI_Census_CountBuildings", 0x0055be00, "thiscall", [], presets=census_buildings,
         globals=ai_map_world, max_cases=400),
    Spec("AI_Census_ComputeTeamStrength", 0x0055bea0, "thiscall", [], presets=census_players,
         globals=ai_map_world, max_cases=400),
    Spec("AI_StartPositionWalkRegion", 0x0055b6e0, "stdcall", [below(20)], presets=start_position,
         globals=ai_map_world),
    Spec("AI_PtrVector_Size", 0x0055bfc0, "thiscall", [], presets=ptr_vector),
    Spec("AI_UninitializedCopyPtrs", 0x0055bff0, "stdcall", [COPY_RANGE.first, COPY_RANGE.last, COPY_RANGE.dest]),
    Spec("AI_CEvaLand_SumAllCells", 0x005546c0, "thiscall", [], presets=grid_world_or_empty, max_cases=500),
    # The agent constructors: EAX is not used (the factory 0x56f6d0 returns the object).
    Spec("GLOBAL_EXPAND_EMPIRE_AGENT_Construct", 0x0056cd00, "thiscall", [], eax_mask=0),
    Spec("COMPUTER_PLAYER_AGENT_Construct", 0x00584680, "thiscall", [], eax_mask=0),
    Spec("T_MAP_WORKER_AGENT_Construct", 0x0057cda0, "thiscall", [], eax_mask=0),
    Spec("T_ATTACK_AGENT_Construct", 0x0055ebe0, "thiscall", [], eax_mask=0),
    Spec("T_ECONOMY_AGENT_Construct", 0x00567850, "thiscall", [], eax_mask=0),
    Spec("T_SAFETY_AGENT_Construct", 0x00586830, "thiscall", [], eax_mask=0),
    Spec("CCentrumCheckAgent_Construct", 0x00560dd0, "thiscall", [], eax_mask=0),
    Spec("std_multimap_end_580220", 0x00580220, "thiscall", [scratch(0)]),
    Spec("std_multimap_empty_580230", 0x00580230, "thiscall", [],
         presets=lambda rng, pool, args: [(0xC, 0, 4)] if rng.random() < 0.4 else []),
    Spec("AI_Pair5_Construct", 0x0059c5a0, "thiscall", [scratch(0), scratch(1)]),
    Spec("AI_CountPlayersTakingPart", 0x00572370, "stdcall", [], globals=ai_world,
         presets=lambda rng, pool, args: [(AI_PLAYERS + p * 0x148, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4)
                                          for p in range(20)]),
]

# -- computer players, package A3: the note tree, the agents, destruction
# (ai_computer_player.cpp) ---------------------------------------------------
#
# The CComputerPlayer at the world base with a consistent note forest (24
# ids, some free; parents, children and root list agree, as DeleteNote
# needs to end), its four id lists, the 11 agent objects (vtable =
# emu.FAKE_VTABLE, kind mostly its index; most in agents[]), the handlers
# per note type, an AiPlayerData node, the note AddNote adds and a filler
# note for a player whose 300 ids are all taken. Notes have the fake vtable
# too: their slot 0 (scalar deleting destructor) is probed. Agent slot 2
# writes the note types into the caller's stack buffer (an effect; the
# buffer's address is not compared). DeleteNote's spec sometimes makes the
# note an orphan (its parent doesn't list it: the sibling search runs to
# the end).

TREE_NOTES = SCRATCH                              # notes 0..23, 0x20 apart
TREE_AGENTS = SCRATCH + SCRATCH_SLOT_BYTES        # agents 0..10, 0x20 apart
TREE_DATA = SCRATCH + 2 * SCRATCH_SLOT_BYTES      # the AiPlayerData node
TREE_NEW_NOTE = SCRATCH + 3 * SCRATCH_SLOT_BYTES  # the note AddNote adds
TREE_FILLER = TREE_NEW_NOTE + 0x20
TREE_NOTE_COUNT = 24
AI_EVALAND_COUNT = 0x007E0568  # g_aiEvaLandRefCount
AI_EVALAND = 0x007E056C        # g_aiEvaLand
AI_WORLD_GLOBAL = 0x007E3408   # g_aiWorld
AI_MAP_WIDTH = 0x007E3414      # words AI::BuildingArea compares x / y with
AI_MAP_HEIGHT = 0x007E3412


def tree_note(k: int) -> int:
    return TREE_NOTES + 0x20 * k


def tree_agent(k: int) -> int:
    return TREE_AGENTS + 0x20 * k


def tree_note_index(value: int):
    """The note index of a pointer argument, or None."""
    offset = value - WORLD_BASE - TREE_NOTES
    return offset // 0x20 if 0 <= offset < TREE_NOTE_COUNT * 0x20 and offset % 0x20 == 0 else None


def note_tree(rng, required=(), allow_full=True, orphan=None, base=0):
    """The fields; `base`: the player's world offset (0: ECX)."""
    fields = []
    agents = []
    for k in range(11):
        agent = tree_agent(k)
        listed = rng.random() >= 0.125 or (k == 0 and not agents)
        kind = rng.randrange(11) if rng.random() < 0.1 else k
        fields += [(agent, FAKE_VTABLE, 4), (agent + 4, kind, 2), (base + 0x730 + 4 * k, WORLD_BASE + agent if listed else 0, 4)]
        if listed:
            agents.append(k)
    fields += [(base + 0x266 + 2 * t, 0xFFFF if rng.random() < 0.25 else rng.choice(agents), 2) for t in range(6)]
    present, roots, parent_of, first_child, next_sibling = [], [], {}, {}, {}
    for k in range(TREE_NOTE_COUNT):
        if k not in required and rng.random() < 1 / 6:
            continue
        first_child[k] = 0xFFFF
        if present and rng.random() < 0.6:
            parent = rng.choice(present)
            parent_of[k] = parent
            if k == orphan:  # not in its parent's children
                next_sibling[k] = rng.getrandbits(16)
            else:
                next_sibling[k] = first_child[parent]
                first_child[parent] = k
        else:
            parent_of[k] = 0xFFFF
            next_sibling[k] = rng.getrandbits(16)
            roots.append(k)
        present.append(k)

    def subset(chance):
        ids = [k for k in present if rng.random() < chance]
        rng.shuffle(ids)
        return ids

    rng.shuffle(roots)
    lists = {0x4: (100, 0xCC, roots), 0xCE: (50, 0x132, subset(1 / 3)), 0x134: (100, 0x1FC, subset(0.5)),
             0x200: (50, 0x264, subset(0.2))}
    for offset, (slots, count_offset, ids) in lists.items():
        fields += [(base + offset + 2 * k, ids[k] if k < len(ids) else 0xFFFF, 2) for k in range(slots)]
        count = rng.randrange(TREE_NOTE_COUNT + 8) if rng.random() < 0.125 else len(ids)
        fields.append((base + count_offset, count, 2))
    active = set(lists[0x134][2])
    for k in present:
        note = tree_note(k)
        flags = rng.randrange(16) if rng.random() < 0.5 else rng.getrandbits(16)
        if rng.random() < 0.9:
            flags = flags | 4 if k in active else flags & ~4
        fields += [(note, FAKE_VTABLE, 4), (note + 4, flags, 2), (note + 6, rng.randrange(6), 2), (note + 8, k, 2),
                   (note + 0x10, parent_of[k], 2), (note + 0x12, next_sibling[k], 2), (note + 0x14, first_child[k], 2),
                   (note + 0x16, 0xFFFF if rng.random() < 0.25 else rng.choice(agents), 2),
                   (note + 0x18, rng.choice(agents), 2)]
    full = allow_full and rng.random() < 1 / 16
    for k in range(300):
        value = WORLD_BASE + tree_note(k) if k in parent_of else WORLD_BASE + TREE_FILLER if full else 0
        fields.append((base + 0x280 + 4 * k, value, 4))
    fields += [(TREE_FILLER, FAKE_VTABLE, 4), (TREE_FILLER + 6, 0, 2), (TREE_NEW_NOTE, FAKE_VTABLE, 4),
               (base + 0x1FE, rng.randrange(103), 2), (base + 0x770, rng.randrange(64), 4),
               (base + 0x778, WORLD_BASE + TREE_DATA, 4)]
    zeros = rng.random() < 0.25
    for t in range(57):
        r = rng.random()
        if zeros:
            score = rng.randrange(3) if r < 0.25 else 0
        else:
            score = 0 if r < 0.35 else rng.randrange(2000) if r < 0.6 else 65000 if r < 0.75 else rng.getrandbits(16)
        fields.append((TREE_DATA + 0x26 + 6 * t, score, 2))
    fields += [(TREE_DATA + 0x10, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4),
               (TREE_DATA + 0x14, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4)]
    return fields


def tree_note_arg(rng, pool):
    return WORLD_BASE + tree_note(rng.randrange(TREE_NOTE_COUNT))


def tree_note_or_null(rng, pool):
    return 0 if rng.random() < 0.25 else tree_note_arg(rng, pool)


def tree_agent_arg(rng, pool):
    return WORLD_BASE + tree_agent(rng.randrange(11))


def tree_agent_or_null(rng, pool):
    return 0 if rng.random() < 0.25 else tree_agent_arg(rng, pool)


def tree_parent_id(rng, pool):
    value = 0xFFFF if rng.random() < 0.35 else rng.randrange(TREE_NOTE_COUNT)
    return value | (rng.getrandbits(16) << 16) if rng.random() < 0.3 else value


def tree_with_notes(*positions):
    """The tree, with the notes the given arguments point at present."""
    def presets(rng, pool, args):
        required = {tree_note_index(args[i]) for i in positions} - {None}
        return note_tree(rng, required)
    return presets


def tree_for_add_note(rng, pool, args):
    parent = args[1] & 0xFFFF
    return note_tree(rng, {parent} if parent != 0xFFFF else ())


def tree_for_delete(rng, pool, args):
    """Half the cases: the id is present (sometimes an orphan; a full
    player possible); otherwise any id below 24 and never full (a free id
    would be the filler)."""
    k = args[0] & 0xFFFF
    if rng.random() < 0.5:
        return note_tree(rng, {k}, orphan=k if rng.random() < 0.3 else None)
    return note_tree(rng, allow_full=False)


TREE_OTHER_PLAYER = SCRATCH + 4 * SCRATCH_SLOT_BYTES  # g_aiPlayer for the note callbacks


def note_at_base(rng, pool, args):
    """The note callbacks: ECX = the note at the world base, its player
    (g_aiPlayer) in scratch slot 4 with the tree. The note's requester is
    sometimes none; its parent a tree note or none (then the original reads
    notes[0xffff], lazily random); its handler an agent."""
    fields = note_tree(rng, base=TREE_OTHER_PLAYER)
    agents = [k for k in range(11) if any(o == TREE_OTHER_PLAYER + 0x730 + 4 * k and v for o, v, _ in fields)]
    present = sorted({o for o, v, _ in fields if TREE_OTHER_PLAYER + 0x280 <= o < TREE_OTHER_PLAYER + 0x280 + 4 * TREE_NOTE_COUNT
                      and v == WORLD_BASE + tree_note((o - TREE_OTHER_PLAYER - 0x280) // 4)})
    parent = 0xFFFF if rng.random() < 0.3 or not present else (rng.choice(present) - TREE_OTHER_PLAYER - 0x280) // 4
    return fields + [(0x10, parent, 2), (0x16, 0xFFFF if rng.random() < 0.3 else rng.choice(agents), 2),
                     (0x18, rng.choice(agents), 2)]


def note_player_global(rng, pool, args):
    return [(AI_PLAYER, WORLD_BASE + TREE_OTHER_PLAYER, 4)]


def eva_land_users(rng, pool, args):
    """CEvaLand::Release: mostly other users left; sometimes the last one,
    with no instance to free."""
    count = 1 if rng.random() < 0.25 else rng.randint(2, 6)
    return [(AI_EVALAND_COUNT, count, 4), (AI_EVALAND, 0, 4)]


def write_note_types(machine, ecx, args, h):
    """Agent slot 2: the note types it handles, into the caller's buffer
    (50 words; types 6..8 reach past handlers[])."""
    r = random.Random(h)
    machine.guest_write(args[0], struct.pack("<50H", *(r.randrange(9) for _ in range(50))))


def note_type_count(h):
    count = h % 51 if (h >> 40) & 7 == 0 else (h >> 8) % 4
    return (h & 0xFFFF0000) | count  # callers use AX


def agent_answer(h):
    """Slots 3 and 4: 0, 1, kAgentDone (100) or anything."""
    return [0, 1, (h & 0xFFFF0000) | 100, h & 0xFFFFFFFF][h & 3]


TREE_VIRTUALS = {
    0x00: 1,                                       # scalar deleting destructor (flags)
    0x08: (1, {0: ()}, {"returns": note_type_count}),  # note types into a stack buffer
    0x0C: (1, {}, {"returns": agent_answer}),      # take a new note
    0x10: (1, {}, {"returns": agent_answer}),      # work on an active note
    0x14: 0,                                       # busy?
    0x1C: 0,                                       # wants to run?
    0x20: 0,                                       # run
    0x24: 4,                                       # a note reports back
    0x28: 1,                                       # cancel a note
}
TREE_EFFECTS = {0x08: write_note_types}


def tree_spec(name, address, args, presets, eax_mask=0, **kwargs):
    return Spec(name, address, "thiscall", args, presets=presets, eax_mask=eax_mask, virtuals=TREE_VIRTUALS,
                virtual_effects=TREE_EFFECTS, **kwargs)


def best_building_world(rng, pool, args):
    """StartBestBuilding: the tree and up to 8 buildings, mostly the
    player's, some construction sites, mostly in the current area, so that
    0x59dbb0 (run as original code) counts 0..8 sites."""
    fields = note_tree(rng)
    player = rng.randrange(20)
    area = rng.getrandbits(16)
    fields += [(0x76C, player | (rng.getrandbits(24) << 8), 4), (0x77C, area | (rng.getrandbits(16) << 16), 4)]
    count = rng.randrange(9)
    fields.append((0x1499E60, count, 4))
    for building_id in range(1, count + 1):
        b = AI_BUILDINGS + building_id * 0x3A
        x, y = rng.randrange(64), rng.randrange(64)
        fields += [(b, player if rng.random() < 0.8 else rng.choice([0xFF, rng.randrange(20)]), 1),
                   (b + 0x21, rng.getrandbits(8) | 1 if rng.random() < 0.8 else 0, 1),
                   (b + 0xA, x, 2), (b + 0xC, y, 2),
                   (TILES + (x * 0x300 + y) * 24 + 0x14, area if rng.random() < 0.8 else rng.getrandbits(16), 2)]
    return fields


def best_building_globals(rng, pool, args):
    return [(AI_WORLD_GLOBAL, WORLD_BASE, 4), (AI_MAP_WIDTH, 64, 2), (AI_MAP_HEIGHT, 64, 2),
            (AI_PLAYER_DATA, WORLD_BASE + TREE_DATA, 4)]


def result_arg(rng, pool):
    return rng.choice([0, 100, rng.getrandbits(32)])

SPECS += [
    tree_spec("CComputerPlayer_AddNote", 0x00551270, [scratch(3), tree_parent_id, tree_agent_or_null],
              tree_for_add_note, eax_mask=0xFFFFFFFF),
    tree_spec("CComputerPlayer_AddNoteUnder", 0x00551230, [scratch(3), tree_note_or_null, tree_agent_or_null],
              tree_with_notes(1), eax_mask=0xFFFFFFFF),
    tree_spec("CComputerPlayer_DeleteNote", 0x00551330, [id_with_noise(TREE_NOTE_COUNT)], tree_for_delete,
              eax_mask=0xFFFFFFFF),
    tree_spec("CComputerPlayer_AddAgent", 0x00551400, [tree_agent_arg], tree_with_notes()),
    tree_spec("CComputerPlayer_DeleteAgent", 0x00551490, [tree_agent_arg], tree_with_notes()),
    tree_spec("CComputerPlayer_DispatchNote", 0x00551510, [tree_note_arg], tree_with_notes(0)),
    tree_spec("CComputerPlayer_DispatchPendingNotes", 0x005514c0, [], tree_with_notes()),
    tree_spec("CComputerPlayer_RunAgents", 0x00551590, [], tree_with_notes()),
    tree_spec("CComputerPlayer_WorkOnNote", 0x005516e0, [tree_note_or_null], tree_with_notes(0)),
    tree_spec("CComputerPlayer_WorkOnNextActiveNote", 0x00551670, [], tree_with_notes()),
    tree_spec("CComputerPlayer_DeleteDoomedNotes", 0x00551790, [], tree_with_notes()),
    tree_spec("CComputerPlayer_UpdateBlackboard", 0x005517d0, [], tree_with_notes()),
    # The function ends with the tail call at 0x550f78 (nop padding after it).
    tree_spec("CComputerPlayer_Shutdown", 0x00550f30, [], tree_with_notes(), globals=eva_land_users,
              cover_until=0x00550f7d),
    tree_spec("CComputerPlayer_Destroy", 0x005509d0, [], tree_with_notes(), globals=eva_land_users),
    # Bit 0 frees the player (the emulator's operator delete, compared).
    tree_spec("CComputerPlayer_DeletingDestroy", 0x005509b0, [anything], tree_with_notes(), globals=eva_land_users,
              eax_mask=0xFFFFFFFF),
    # 0x59dbb0 and 0x58ecb0 (package A2) run as original code on both sides.
    tree_spec("CComputerPlayer_StartBestBuilding", 0x00551a70, [], best_building_world,
              globals=best_building_globals),
    # AX only for a note without requester (the upper half is the caller's EAX; callers use AX).
    tree_spec("BLACK_BOARD_NOTE_ReportToRequester", 0x00582480, [result_arg, anything], note_at_base,
              eax_mask=0xFFFF, globals=note_player_global),
    tree_spec("BLACK_BOARD_NOTE_ExecuteAtHandler", 0x00582510, [], note_at_base, eax_mask=0xFFFFFFFF,
              globals=note_player_global),
]

# -- package A3 batch 2: the request agents' constructors, the request notes'
# set-up and destruction (ai_request_notes.cpp); the object at the world
# base. The agent constructors' EAX is not used (the factory returns the
# object; its upper half is the caller's). Bit 0 of a deleting destructor's
# flags frees the object (the emulator's operator delete, compared).
SPECS += [
    Spec("LOCAL_WARE_REQUEST_AGENT_Construct", 0x0057ab50, "thiscall", [], eax_mask=0),
    Spec("HOUSE_GROUND_REQUEST_AGENT_Construct", 0x005764f0, "thiscall", [], eax_mask=0),
    Spec("HOUSE_REQUEST_AGENT_Construct", 0x005787a0, "thiscall", [], eax_mask=0),
    Spec("T_DO_BUILD_HOUSE_AGENT_Construct", 0x005654b0, "thiscall", [], eax_mask=0),
    Spec("LOCAL_WARE_REQUEST_NOTE_Init", 0x0057ab20, "thiscall", [anything, anything, anything]),
    Spec("HOUSE_REQUEST_NOTE_Init", 0x00578760, "thiscall", [anything, anything, anything, anything]),
    Spec("T_DO_BUILD_HOUSE_NOTE_Init", 0x00565470, "thiscall", [anything, anything, anything, anything]),
    Spec("T_DO_BUILD_HOUSE_NOTE_Construct", 0x005801c0, "thiscall", [anything, anything, anything, anything]),
    Spec("BLACK_BOARD_NOTE_Construct", 0x005801b0, "thiscall", []),
    Spec("BLACK_BOARD_NOTE_Destroy", 0x00565590, "thiscall", [], eax_mask=0),
    Spec("BLACK_BOARD_NOTE_DeletingDestroy", 0x005655a0, "thiscall", [anything]),
    Spec("RequestNote_DeletingDestroy", 0x0056f6b0, "thiscall", [anything]),
]


# -- batch 27: the shared land evaluation object (ai_evaland.cpp) ------------
#
# The object at world offset 0 (grid pointer +0x158 into scratch or null).
# The CRT's eh vector destructor iterator (0x61f18a) is a callee probe: it
# would run the original list destructor 0x5591e0 on the 20 lists, which
# needs real list nodes (in-process tests cover that); here only its
# arguments are compared.

EH_VECTOR_DESTRUCTOR = {0x0061f18a: (4, "stdcall", "probe")}


def evaland_object(rng, pool, args):
    return [(0x158, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]


def evaland_globals(rng, pool, args):
    return [(0x007e0568, rng.choice([1, 1, 2, 3, 0, rng.getrandbits(32)]), 4),
            (0x007e056c, rng.choice([WORLD_BASE, WORLD_BASE, 0]), 4),
            (0x007e3134, rng.getrandbits(32), 4)]

SPECS += [
    Spec("AI_CEvaLand_Destroy", 0x00553fe0, "thiscall", [], eax_mask=0, presets=evaland_object,
         globals=evaland_globals, callees=EH_VECTOR_DESTRUCTOR),
    Spec("AI_CEvaLand_Release", 0x00553dc0, "stdcall", [], presets=evaland_object, globals=evaland_globals,
         callees=EH_VECTOR_DESTRUCTOR),
]

# -- computer players A2: T_ECONOMY_AGENT conditions (ai_economy_conditions.cpp) --
#
# The agent (EconomyAgent, 0xa0 bytes) sits at world offset 0 (ECX = the
# world), g_aiPlayerData points at scratch 0, g_aiWorld at the world. The
# conditions call AI::RecordBestCandidate, AI_CountBuildings (buildings of
# the types they count, on a 400 x 400 map) and AI_AreaGoodsAmount (short
# pile lists in the agent's area). EAX is not used by 0x56a1a0.

AI_PLAYER_NUMBER = 0x007e2eb8
ECONOMY_SLOPES = (0x007e1290, 0x007e12b0)  # g_aiWantedWeaponsmithSlopes, g_aiWantedGoldMineSlopes


def economy_globals(rng, pool, args):
    fields = [(AI_WORLD, WORLD_BASE, 4), (AI_PLAYER_DATA, WORLD_BASE + SCRATCH, 4),
              (AI_PLAYER_NUMBER, rng.randrange(3), 1), (AI_MAP_SIZES, 400, 2), (AI_MAP_SIZES + 2, 400, 2)]
    for table in ECONOMY_SLOPES:
        for s in range(4):
            if rng.random() < 0.1:
                bits = rng.getrandbits(32)
            else:
                bits = struct.unpack("<I", struct.pack("<f", rng.randrange(1000) / 997.0))[0]
            fields.append((table + 4 * s, bits, 4))
    return fields


def economy_pile_chain(rng, base, length, storehouse_kinds):
    """(head, fields) of a chain of distinct piles from id `base`."""
    fields, head = [], 0
    stride = rng.randint(1, 3)
    for k in range(length):
        pile = base + k * stride
        record = AI_PILES + pile * 14
        art = 0 if storehouse_kinds and rng.random() < 0.5 else rng.randrange(256)
        fields += [(record + 1, rng.randrange(5), 1), (record + 4, art, 1), (record + 0xC, head, 2)]
        head = pile
    return head, fields


def economy_agent(rng, pool, args):
    area = rng.randrange(4)
    race = rng.randrange(4) if rng.random() < 0.9 else rng.getrandbits(32)
    fields = [(8, race, 4), (0xC, area, 2)]
    for offset in range(0x42, 0xA0, 2):
        r = rng.random()
        value = 0 if r < 0.35 else rng.getrandbits(16) if r < 0.4 else rng.randrange(9)
        fields.append((offset, value, 2))
    if rng.random() < 0.25:
        fields += [(0x42, rng.randrange(100), 2), (0x44, rng.randrange(100), 2), (0x46, rng.randrange(40), 2),
                   (0x48, rng.randrange(12), 2)]
    # The population: every stage and crowd limit about equally often.
    low, high = rng.choice(((0, 40), (40, 101), (101, 251), (251, 501), (501, 2000), (2000, 2500), (0, 0x10000)))
    fields.append((0x3E, rng.randrange(low, high), 2))
    # AiPlayerData: the toolsmith order, unk550, the storehouse count.
    if rng.random() < 0.5:
        fields.append((SCRATCH + 0x26 + 6 * (57 + 12), 0, 2))
    if rng.random() < 0.25:
        fields.append((SCRATCH + 0x550, 0, 4))
    fields.append((SCRATCH + 0x344 + 2 * 1, rng.randrange(3), 2))
    # Buildings for AI_CountBuildings.
    count = rng.randrange(30)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x, y = rng.randrange(400), rng.randrange(400)
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1),
                   (record + 6, rng.choice((1, 12, 23, 24)), 1),
                   (record + 0x21, int(rng.random() < 0.25), 1), (record + 0xA, x, 2), (record + 0xC, y, 2),
                   (TILES + (x * 0x300 + y) * 24 + 0x14, area if rng.random() < 0.6 else rng.randrange(4), 2)]
    # Pile lists of the area: gold ore, swords, bows, spears and the area's own list.
    base = rng.randint(1, 1000)
    record = AI_AREAS + area * 0x2E0
    for goods in (6, 18, 19, 20):
        head, chain = economy_pile_chain(rng, base, rng.randrange(9), False)
        fields += chain + [(record + 0xA + goods * 2, head, 2)]
        base += 40
    head, chain = economy_pile_chain(rng, base, rng.randrange(8), True)
    fields += chain + [(record + 8, head, 2)]
    return fields


def economy_condition(name: str, address: int) -> Spec:
    return Spec(name, address, "thiscall", [], eax_mask=0, presets=economy_agent, globals=economy_globals,
                max_cases=1000)

SPECS += [
    economy_condition("T_ECONOMY_AGENT_ConsiderSawmill", 0x00568250),
    economy_condition("T_ECONOMY_AGENT_ConsiderForester", 0x005682b0),
    economy_condition("T_ECONOMY_AGENT_ConsiderToolsmith", 0x00569160),
    economy_condition("T_ECONOMY_AGENT_ConsiderWeaponsmith", 0x005691b0),
    economy_condition("T_ECONOMY_AGENT_ConsiderRiceFarm", 0x00569330),
    economy_condition("T_ECONOMY_AGENT_ConsiderFishery", 0x005693c0),
    economy_condition("T_ECONOMY_AGENT_ConsiderFarm", 0x00569430),
    economy_condition("T_ECONOMY_AGENT_ConsiderMill", 0x00569490),
    economy_condition("T_ECONOMY_AGENT_ConsiderBakery", 0x005694f0),
    economy_condition("T_ECONOMY_AGENT_ConsiderPigFarm", 0x00569520),
    economy_condition("T_ECONOMY_AGENT_ConsiderSlaughterhouse", 0x00569590),
    economy_condition("T_ECONOMY_AGENT_ConsiderWaterworks", 0x005695e0),
    economy_condition("T_ECONOMY_AGENT_ConsiderBarracks", 0x00569640),
    economy_condition("T_ECONOMY_AGENT_ConsiderGoldMine", 0x005696d0),
    economy_condition("T_ECONOMY_AGENT_ConsiderGoldSmelter", 0x00569860),
    economy_condition("T_ECONOMY_AGENT_ConsiderStorehouse", 0x00569900),
    economy_condition("T_ECONOMY_AGENT_ConsiderHospital", 0x005699d0),
    economy_condition("T_ECONOMY_AGENT_ConsiderGemMine", 0x00569a00),
    economy_condition("T_ECONOMY_AGENT_ConsiderSulfurMine", 0x00569a30),
    economy_condition("T_ECONOMY_AGENT_ConsiderPowderMaker", 0x00569aa0),
    economy_condition("T_ECONOMY_AGENT_ConsiderCharcoalBurner", 0x00569b00),
    economy_condition("T_ECONOMY_AGENT_ConsiderSmallTemple", 0x00569b30),
    economy_condition("T_ECONOMY_AGENT_ConsiderLargeTemple", 0x00569c00),
    economy_condition("T_ECONOMY_AGENT_ConsiderApiary", 0x00569c90),
    economy_condition("T_ECONOMY_AGENT_ConsiderDrinkBuilding", 0x00569d20),
]

# -- computer players A2: map queries (ai_map_queries.cpp) ------------------
#
# Coordinates are words at scratch 0 (x) and scratch 1 (y), near and past
# the edges of a random AI map size; the tile's fields are pinned. Off the
# map the original leaves its caller's EAX upper half (AX compared only).


def ai_query_map(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_WORLD, WORLD_BASE, 4), (AI_PLAYER_DATA, WORLD_BASE + SCRATCH, 4),
            (AI_MAP_SIZES, rng.randint(1, 0x2FF), 2), (AI_MAP_SIZES + 2, rng.randint(1, 0x2FF), 2)]


def query_coordinate(rng):
    r = rng.random()
    if r < 0.15:
        return rng.randint(0x2F0, 0x2FF)
    if r < 0.2:
        return rng.getrandbits(16)
    return rng.randrange(0x2F0)


def query_tile(rng, x, y):
    t = TILES + (x * 0x300 + y) * 24
    obj = rng.choice((0x43, 0x44, 0x53, 0x54, 0x72, 0x73, 0x7E, 0x7F, 0)) if rng.random() < 0.5 else rng.getrandbits(8)
    return [(t + 2, 0 if rng.random() < 0.25 else rng.getrandbits(16), 2), (t + 7, obj, 1),
            (t + 0xA, rng.getrandbits(8), 1), (t + 0x11, rng.getrandbits(8), 1)]


def ai_query_coords(rng, pool, args):
    """x, y at scratch 0 / 1 and the tile there (only tiles inside the
    0x300 x 0x300 array)."""
    x, y = query_coordinate(rng), query_coordinate(rng)
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2)]
    if x < 0x300 and y < 0x300:
        fields += query_tile(rng, x, y)
    return fields


def ai_query_tile_any(rng, pool, args):
    """For the queries without a bounds check: x, y inside the tile array."""
    x, y = rng.randrange(0x300), rng.randrange(0x300)
    return [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2)] + query_tile(rng, x, y)


def ai_query_record(rng, pool, args):
    """A building record at scratch 0 (door x / y at +0xa / +0xc)."""
    x, y = query_coordinate(rng), query_coordinate(rng)
    return [(SCRATCH + 0xA, x, 2), (SCRATCH + 0xC, y, 2)]


class WalkRow:
    """x, y values for AI_WalkRegionAlongRow (upper halves sometimes set)
    and walk regions along the row, mostly 0."""

    def __init__(self):
        self.xy = (0, 0)

    def x(self, rng, pool):
        self.xy = (query_coordinate(rng), query_coordinate(rng))
        return self.xy[0] | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)

    def y(self, rng, pool):
        return self.xy[1] | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)

    def presets(self, rng, pool, args):
        x, y = args[0] & 0xFFFF, args[1] & 0xFFFF
        fields = []
        for k in range(20):
            if x + k < 0x300 and y < 0x300:
                fields.append((TILES + ((x + k) * 0x300 + y) * 24 + 0x10, 0 if rng.random() < 0.8 else rng.getrandbits(8), 1))
        return fields


WALK_ROW = WalkRow()


def ai_query_sites(rng, pool, args):
    fields = count_buildings_world(rng, pool, args)
    return fields + [(SCRATCH, rng.randrange(3), 1), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(4), 2)]


def ai_query_settlers(rng, pool, args):
    """A settler list of up to 9 in the area's list for the type."""
    settler_type, area = rng.randrange(48), rng.randrange(4096)
    fields = [(SCRATCH, settler_type, 1), (SCRATCH + SCRATCH_SLOT_BYTES, area, 2)]
    start, head = rng.randint(1, 20000), 0
    for k in range(rng.randrange(10)):
        settler = start + 7 * k
        fields.append((SETTLERS + settler * 0x40 + 0x10, head, 2))
        head = settler
    return fields + [(AI_AREAS + area * 0x2E0 + 0x12A + settler_type * 2, head, 2)]


def ai_query_teams(rng, pool, args):
    fields = []
    for p in range(20):
        record = AI_PLAYERS + p * 0x148
        active = 1 if rng.random() < 0.5 else rng.choice((0, 2, rng.getrandbits(32)))
        fields += [(record, active, 4), (record + 0xC, rng.randrange(4), 4)]
    return fields


def tower_type(rng, pool):
    return rng.choice((14, 15, 16, 17, 18, 19, 0)) if rng.random() < 0.7 else rng.getrandbits(16)

SPECS += [
    Spec("AI_BuildingAt", 0x00599510, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=ai_query_coords, globals=ai_query_map),
    Spec("AI_AreaAt", 0x005995c0, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=ai_query_coords, globals=ai_query_map),
    Spec("AI_BuildingRecordArea", 0x00599670, "fastcall", [scratch(0)], eax_mask=0xFFFF,
         presets=ai_query_record, globals=ai_query_map),
    Spec("AI_WalkRegionAlongRow", 0x00599870, "fastcall", [WALK_ROW.x, WALK_ROW.y], eax_mask=0xFFFF,
         presets=WALK_ROW.presets, globals=ai_query_map),
    Spec("AI_StoneAmountAt", 0x00599910, "fastcall", [scratch(0), scratch(1)], presets=ai_query_tile_any,
         globals=ai_query_map),
    Spec("AI_IsTreeAt", 0x00599950, "fastcall", [scratch(0), scratch(1)], presets=ai_query_tile_any,
         globals=ai_query_map),
    Spec("AI_ResourceAmountAt", 0x00599990, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=ai_query_coords, globals=ai_query_map),
    Spec("AI_ResourceKindAt", 0x005999e0, "fastcall", [scratch(0), scratch(1)], eax_mask=0xFFFF,
         presets=ai_query_coords, globals=ai_query_map),
    Spec("AI_TerritoryRadius", 0x0058ed30, "fastcall", [scratch(0)], globals=ai_query_map,
         presets=lambda rng, pool, args: [(SCRATCH, tower_type(rng, pool), 2)]),
    # AL only: the rest of EAX is the caller's.
    Spec("AI_SiteKindOf", 0x0058ed70, "fastcall", [small_with_noise(0, 40)], eax_mask=0xFF),
    Spec("AI_TakeBestCandidate", 0x0058ecb0, "fastcall", [id_with_noise(0x60)], globals=ai_query_map),
    Spec("AI_CountSitesInArea", 0x0059dbb0, "fastcall", [scratch(0), scratch(1)], presets=ai_query_sites,
         globals=ai_world_400, max_cases=600),
    Spec("AI_CountAreaSettlersOfType", 0x0059dc70, "fastcall", [scratch(0), scratch(1)],
         presets=ai_query_settlers, globals=ai_world),
    Spec("AI_TeamHasActivePlayer", 0x00593140, "stdcall", [below(20)], presets=ai_query_teams,
         globals=ai_map_world),
]

# -- computer players A2, batch 3: site queries (ai_site_queries.cpp) --------

AI_NOTES_PLAYER = SCRATCH + 2 * SCRATCH_SLOT_BYTES   # a ComputerPlayer (0x780 bytes)
AI_SITE_NOTES = SCRATCH + 3 * SCRATCH_SLOT_BYTES     # 300 notes of 0x24 bytes


def site_notes(rng, pool, args):
    """g_aiPlayer's 300 note pointers (a third null) to notes of types 0..5
    with a building type 0..3 and a position, and the tile areas there."""
    fields = [(SCRATCH, rng.randrange(4), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(300), 2)]
    for n in range(300):
        note = AI_SITE_NOTES + n * 0x24
        pointer = 0 if rng.random() < 0.33 else WORLD_BASE + note
        x, y = rng.randrange(410), rng.randrange(400)
        fields += [(AI_NOTES_PLAYER + 0x280 + 4 * n, pointer, 4), (note + 6, rng.randrange(6), 2),
                   (note + 0x1C, rng.randrange(4), 2), (note + 0x1E, x, 2), (note + 0x20, y, 2)]
        if x < 400:
            fields.append((TILES + (x * 0x300 + y) * 24 + 0x14, 0 if rng.random() < 0.5 else rng.randrange(300), 2))
    return fields


def site_notes_globals(rng, pool, args):
    return ai_world_400(rng, pool, args) + [(AI_PLAYER, WORLD_BASE + AI_NOTES_PLAYER, 4)]


def near_buildings(rng, pool, args):
    """Up to 40 buildings of players 0..2 near the origin; some military
    with a garrison."""
    count = rng.randrange(40)
    fields = [(BUILDING_COUNT, count, 4)]
    for building in range(1, count + 2):
        record = BUILDINGS + building * BUILDING_BYTES
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1),
                   (record + 2, rng.randrange(60), 2), (record + 4, rng.randrange(60), 2),
                   (record + 6, rng.choice((2, 3, 15, 16, 18, 24)), 1),
                   (record + 8, 0 if rng.random() < 0.5 else rng.randint(1, 100), 2)]
    return fields


def near_type(rng, pool):
    return rng.choice((0xFFFF, 15, 2, 18)) | (rng.getrandbits(16) << 16 if rng.random() < 0.2 else 0)


def with_noise(generator):
    def generate(rng, pool):
        return generator(rng, pool) | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)
    return generate


def basic_economy(rng, pool, args):
    player = rng.randrange(20)
    race = rng.randrange(5) if rng.random() < 0.9 else rng.getrandbits(32)
    fields = [(SCRATCH, player, 2), (AI_PLAYERS + player * 0x148 + 4, race, 4)]
    data = SCRATCH + SCRATCH_SLOT_BYTES
    fields += [(data + 0x344 + 2 * t, 0 if rng.random() < 0.2 else 1, 2) for t in range(57)]
    return fields

SPECS += [
    Spec("AI_HasSiteNoteInArea", 0x00571d00, "fastcall", [scratch(0), scratch(1)], presets=site_notes,
         globals=site_notes_globals, max_cases=300),
    Spec("AI_HasBuildingNear", 0x00571d90, "fastcall",
         [near_type, with_noise(below(60)), with_noise(below(60)), with_noise(below(40)), below(3)],
         presets=near_buildings, globals=ai_world, max_cases=600),
    Spec("AI_CountBuildingsNear", 0x00571e30, "fastcall",
         [near_type, with_noise(below(60)), with_noise(below(60)), with_noise(below(40)), below(3)],
         presets=near_buildings, globals=ai_world, max_cases=600),
    Spec("AI_NextGarrisonedMilitaryBuilding", 0x00571ee0, "fastcall",
         [lambda rng, pool: 0xFFFF if rng.random() < 0.1 else rng.randrange(45), with_noise(below(3))],
         presets=near_buildings, globals=ai_world, max_cases=600),
    Spec("AI_CheckBasicEconomy", 0x005720c0, "fastcall", [scratch(0), anything, scratch(1)],
         presets=basic_economy, globals=ai_world),
    Spec("AI_ResetUnitGroup", 0x005930f0, "stdcall", [below(20), below(10)]),
    Spec("AI_ArmyState_Reset", 0x00593f50, "thiscall", []),
]


# --- session 51: AI A2 leftovers (wip/ai-a2) ---

AI_LAND_EVALUATION = 0x007e3134  # g_aiLandEvaluation (a CEvaLand pointer)
AI_HUNTER_VECTORS = 0x007e368c   # g_aiHunterVectors: 20 x {begin, end, ...}, stride 16


def army_hunter_vectors(rng, pool, args):
    """g_aiHunterVectors with random begin/end pointers (the reset sets end =
    begin)."""
    fields = []
    for p in range(20):
        fields += [(AI_HUNTER_VECTORS + 16 * p, rng.getrandbits(32), 4),
                   (AI_HUNTER_VECTORS + 16 * p + 4, rng.getrandbits(32), 4)]
    return fields


NODE_LAND = SCRATCH + 2 * SCRATCH_SLOT_BYTES    # the CEvaLand (lists at +0, 12 bytes each)
NODE_HEADS = SCRATCH + 3 * SCRATCH_SLOT_BYTES   # 20 list heads of 8 bytes
NODE_SLOTS = 6                                  # AiPlayerData nodes in scratch slots 4..9


def node_buildings(rng, pool, args):
    """Buildings with door tiles and areas (count_buildings_world), the
    player word (scratch 0, < 4) and area word (scratch 1, < 5), and the
    players' node lists: each node (area word +8 < 5) in the list of one of
    the players 0..2."""
    fields = count_buildings_world(rng, pool, args)
    fields += [(SCRATCH, rng.randrange(4), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(5), 2)]
    last = {}
    for p in range(20):
        head = NODE_HEADS + 8 * p
        fields.append((NODE_LAND + 12 * p + 4, WORLD_BASE + head, 4))
        last[p] = head
    for n in range(NODE_SLOTS):
        node = SCRATCH + (4 + n) * SCRATCH_SLOT_BYTES
        p = rng.randrange(3)
        fields += [(last[p], WORLD_BASE + node, 4), (node + 8, rng.randrange(5), 2)]
        last[p] = node
    for p in range(20):
        fields.append((last[p], WORLD_BASE + NODE_HEADS + 8 * p, 4))
    return fields


def node_buildings_globals(rng, pool, args):
    return ai_world_400(rng, pool, args) + [(AI_LAND_EVALUATION, WORLD_BASE + NODE_LAND, 4)]


def wrapped_pos(rng, pool, args):
    """A WrappedPos at world +0 (ECX): sizes above 0 (the original divides
    by them), sometimes from 0x8000 on, often on the grid's edges; the
    coordinates WrapCoords gets in scratch 0 and 1."""
    def size():
        roll = rng.random()
        if roll < 0.1:
            return rng.randrange(0x7FF0, 0x8010)
        if roll < 0.2:
            return rng.randrange(1, 0x10000)
        return rng.randrange(1, 50)
    width, height = size(), size()
    def coordinate(limit):
        roll = rng.random()
        if roll < 0.3:
            return rng.choice((0, limit - 1, limit, limit + 1)) & 0xFFFF
        if roll < 0.5:
            return -rng.randrange(1, 200) & 0xFFFF
        if roll < 0.9:
            return rng.randrange(60)
        return rng.getrandbits(16)
    return [(0, coordinate(width), 2), (2, coordinate(height), 2), (4, width, 2), (6, height, 2),
            (SCRATCH, coordinate(width), 2), (SCRATCH + SCRATCH_SLOT_BYTES, coordinate(height), 2)]


def grid_coordinate(rng, pool):
    roll = rng.random()
    if roll < 0.3:
        return rng.randrange(60)
    if roll < 0.5:
        return -rng.randrange(1, 200) & 0xFFFFFFFF
    if roll < 0.8:
        return rng.randrange(1000)
    return rng.getrandbits(32)


def grid_size(rng, pool):
    """A size whose low word is above 0, sometimes with an upper half."""
    value = rng.randrange(1, 60) if rng.random() < 0.8 else rng.randrange(1, 0x10000)
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


SPECS += [
    # 0x591e6e is dead: the inlined vector erase(begin(), end()) copies the
    # elements from end to end (cmp edx, edx; je), i.e. never.
    Spec("AI_Army_Reset", 0x00591e00, "thiscall", [], globals=army_hunter_vectors, exempt_blocks=(0x591e6e,),
         max_cases=200),
    Spec("AI_CountNodeBuildingsByType", 0x00572420, "fastcall", [scratch(0), scratch(1)],
         presets=node_buildings, globals=node_buildings_globals, max_cases=600),
    Spec("AI_WrappedPos_Construct", 0x00597070, "thiscall", [], presets=wrapped_pos),
    Spec("AI_WrappedPos_ConstructXywh", 0x00597090, "thiscall",
         [grid_coordinate, grid_coordinate, grid_size, grid_size], presets=wrapped_pos),
    Spec("AI_WrappedPos_SetPosition", 0x005970c0, "thiscall", [grid_coordinate, grid_coordinate],
         presets=wrapped_pos),
    Spec("AI_WrappedPos_SetSize", 0x005970f0, "thiscall", [grid_size, grid_size], presets=wrapped_pos),
    # EAX is left over from the step's arithmetic; the callers don't use it.
    Spec("AI_WrappedPos_Step", 0x00597110, "thiscall", [with_noise(below(8))], presets=wrapped_pos, eax_mask=0),
    Spec("AI_WrappedPos_Distance", 0x005971f0, "thiscall", [grid_coordinate, grid_coordinate],
         presets=wrapped_pos),
    Spec("AI_WrappedPos_Wrap", 0x00597250, "thiscall", [], presets=wrapped_pos),
    Spec("AI_WrappedPos_WrapCoords", 0x00597260, "thiscall", [scratch(0), scratch(1)], presets=wrapped_pos),
    Spec("AI_WrappedPos_Advance", 0x00597300, "thiscall", [with_noise(below(8)), with_noise(below(40))],
         presets=wrapped_pos),
]


def economy_agent_with_ores(rng, pool, args):
    """economy_agent plus pile lists of iron ore (7) and iron (11) in the
    agent's area (AI_AreaGoodsAmount of the mine and smelter conditions),
    sometimes long enough for more than 10 units."""
    fields = economy_agent(rng, pool, args)
    area = next(value for offset, value, size in fields if offset == 0xC and size == 2)
    record = AI_AREAS + area * 0x2E0
    base = rng.randint(2000, 3000)
    for goods in (7, 11):
        head, chain = economy_pile_chain(rng, base, rng.randrange(12), False)
        fields += chain + [(record + 0xA + goods * 2, head, 2)]
        base += 40
    if rng.random() < 0.3:  # coal around the coal users' demand (every grade)
        fields.append((0x46, rng.randrange(60), 2))
    return fields


SPECS += [
    Spec(name, address, "thiscall", [], eax_mask=0, presets=economy_agent_with_ores, globals=economy_globals,
         max_cases=1000)
    for name, address in (("T_ECONOMY_AGENT_ConsiderCoalMine", 0x00568bc0),
                          ("T_ECONOMY_AGENT_ConsiderIronMine", 0x00568d80),
                          ("T_ECONOMY_AGENT_ConsiderIronSmelter", 0x00568f40))
]


AI_CENSUS = 0x007E5EF8  # g_aiCensus: byte +0 settlers_counted, dwords +0x3f4 per player


def snapshot_world(rng, pool, args):
    """The agent at world +0 (race, area < 4), 40 settlers on a small part
    of the map (some on their tile, in one of the areas), and per area its
    player, a carrier list and pile lists of logs, boards, stones, coal, iron
    and sulfur. scratch 1 holds an area for AI_AreaPopulation."""
    race = rng.randrange(4) if rng.random() < 0.9 else rng.getrandbits(32)
    fields = [(8, race, 4), (0xC, rng.randrange(4), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(4), 2),
              (SETTLER_COUNT, 40, 4)]
    for settler in range(1, 42):
        record = SETTLERS + settler * SETTLER_BYTES
        x, y = rng.randrange(60), rng.randrange(60)
        owner = 0xFF if rng.random() < 0.15 else rng.randrange(3)
        art = 0 if rng.random() < 0.3 else rng.randrange(60)
        fields += [(record, owner, 1), (record + 1, art, 1), (record + 4, x, 2), (record + 6, y, 2)]
        if rng.random() < 0.67:
            fields += [(tile(x, y), settler, 2), (tile(x, y) + 0x14, rng.randrange(4), 2)]
    base = rng.randint(1, 1000)
    for area in range(4):
        record = AI_AREAS + area * 0x2E0
        fields.append((record, 0xFF if rng.random() < 0.12 else rng.randrange(3), 1))
        head = 0
        for k in range(rng.randrange(5)):
            settler = 1000 + area * 10 + k
            fields.append((SETTLERS + settler * SETTLER_BYTES + 0x10, head, 2))
            head = settler
        fields.append((record + 0x12A, head, 2))
        for goods in (3, 1, 2, 5, 11, 25):
            head, chain = economy_pile_chain(rng, base, rng.randrange(6), False)
            fields += chain + [(record + 0xA + goods * 2, head, 2)]
            base += 20
    return fields


def snapshot_globals(rng, pool, args):
    fields = economy_globals(rng, pool, args)
    fields.append((AI_CENSUS, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1))
    fields += [(AI_CENSUS + 0x3F4 + 4 * p, rng.getrandbits(32), 4) for p in range(20)]
    return fields


SPECS += [
    # EAX is left over from the last copy; the caller (0x567890) doesn't use it.
    Spec("T_ECONOMY_AGENT_TakeAreaSnapshot", 0x00567910, "thiscall", [], eax_mask=0, presets=snapshot_world,
         globals=snapshot_globals, max_cases=600),
    Spec("AI_AreaPopulation", 0x0059dcc0, "fastcall", [scratch(1)], presets=snapshot_world,
         globals=snapshot_globals, max_cases=600),
]


# -- computer players A2, session 52: building, land and army queries --------
# (ai_building_queries.cpp, ai_land_queries.cpp, ai_army.cpp)

AI_LAND_EVALUATION = 0x007E3134  # g_aiLandEvaluation (CEvaLand*)
MILITARY_AND_OTHER_TYPES = (15, 16, 18, 17, 14, 19, 1, 7)


def sites_of_type(rng, pool, args):
    """count_buildings_world plus the player (scratch 0, byte), type
    (scratch 2, byte) and area (scratch 1, word; often 0) asked for."""
    fields = count_buildings_world(rng, pool, args)
    area = 0 if rng.random() < 0.4 else rng.randrange(4)
    return fields + [(SCRATCH, rng.randrange(3), 1), (SCRATCH + 2 * SCRATCH_SLOT_BYTES, rng.randrange(3), 1),
                     (SCRATCH + SCRATCH_SLOT_BYTES, area, 2)]


def buildings_in_areas(rng, pool, args):
    """Up to 30 building records (high-water mark, sometimes -1) with owners
    0..2, military and other types, buildup 0 or not and door tiles in
    areas 0..3."""
    count = rng.randrange(30)
    fields = [(BUILDING_COUNT, 0xFFFFFFFF if rng.random() < 0.03 else count, 4)]
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x, y = rng.randrange(400), rng.randrange(400)
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1),
                   (record + 6, rng.choice(MILITARY_AND_OTHER_TYPES), 1),
                   (record + 0x21, 0 if rng.random() < 0.6 else rng.randrange(1, 256), 1),
                   (record + 0xA, x, 2), (record + 0xC, y, 2),
                   (TILES + (x * 0x300 + y) * 24 + 0x14, rng.randrange(4), 2)]
    return fields


def mine_record(rng, pool, args):
    """A building id (scratch 0, word) whose worker and dig count are
    sometimes 0."""
    building = rng.randrange(4000)
    record = BUILDINGS + building * BUILDING_BYTES
    fields = [(SCRATCH, building, 2), (record + 0x1A, 0 if rng.random() < 0.2 else rng.randrange(1, 40), 1)]
    if rng.random() < 0.2:
        fields.append((record + 8, 0, 2))
    return fields


def resource_kind(rng, pool):
    """A resource kind (high nibble values), sometimes with noise above."""
    return (rng.randrange(16) << 4) | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def resource_tile(rng, pool, args):
    """The tile (x, y) holds the asked kind half of the time."""
    x, y, kind = args[0] & 0xFFFF, args[1] & 0xFFFF, args[2] & 0xFFFF
    if x < 0x300 and y < 0x300 and rng.random() < 0.5:
        return [(TILES + (x * 0x300 + y) * 24 + 0x11, (kind & 0xF0) | rng.randrange(16), 1)]
    return []


def resource_ring_tiles(rng, pool, args):
    """The tiles around (x, y) (wrapped on a 64 x 64 corner of the map) hold
    the asked kind half of the time."""
    x, y, kind = args[0] & 0xFFFF, args[1] & 0xFFFF, args[2] & 0xFFFF
    fields = []
    for dx in range(-3, 4):
        for dy in range(-3, 4):
            tx, ty = (x + dx) % 64, (y + dy) % 64
            if rng.random() < 0.5:
                fields.append((TILES + (tx * 0x300 + ty) * 24 + 0x11, (kind & 0xF0) | rng.randrange(16), 1))
    return fields


def ai_small_map_world(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randrange(1, 60), 2),
            (AI_MAP_SIZES + 2, rng.randrange(1, 60), 2)]


FARM_LAND = SCRATCH + 3 * SCRATCH_SLOT_BYTES   # the CEvaLand
FARM_CELLS = SCRATCH + 4 * SCRATCH_SLOT_BYTES  # its 8 x 8 grid cells (0xc4 bytes each)


def farm_land(rng, pool, args):
    """player (scratch 0, byte) with race 0..4 (sometimes anything), x / y
    (scratch 1 / 2, words), and the CEvaLand's 8 x 8 grid with edge values
    of the three land shares."""
    player = rng.randrange(20)
    race = rng.randrange(5) if rng.random() < 0.9 else rng.getrandbits(32)
    fields = [(SCRATCH, player, 1), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(100), 2),
              (SCRATCH + 2 * SCRATCH_SLOT_BYTES, rng.randrange(100), 2), (AI_PLAYERS + player * 0x148 + 4, race, 4),
              (FARM_LAND + 0x140, 8, 4), (FARM_LAND + 0x144, 8, 4), (FARM_LAND + 0x158, WORLD_BASE + FARM_CELLS, 4)]
    edges = (9, 10, 11, 39, 40, 41, 0, 255)
    for cell in range(64):
        base = FARM_CELLS + cell * 0xC4
        for offset in (0x1E, 0x1F, 0x0D):
            fields.append((base + offset, rng.choice(edges) if rng.random() < 0.7 else rng.getrandbits(8), 1))
    return fields


def farm_land_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_LAND_EVALUATION, WORLD_BASE + FARM_LAND, 4)]


def sparse_index(rng, pool, args):
    """The index (scratch 0): in the table, at its end or negative."""
    roll = rng.random()
    if roll < 0.2:
        index = rng.randint(19990, 20010)
    elif roll < 0.3:
        index = -rng.randrange(1, 2000)
    elif roll < 0.35:
        index = rng.getrandbits(32)
    else:
        index = rng.randrange(20000)
    return [(SCRATCH, index & 0xFFFFFFFF, 4)]


def fighting_group(rng, pool, args):
    """The unit group's length (0..100, sometimes 0) and members whose job
    is 142 (fighting) with a random share."""
    index = args[0] * 10 + args[1]
    length = 0 if rng.random() < 0.1 else rng.randint(1, 100 if rng.random() < 0.3 else 10)
    share = rng.random()
    fields = [(0x11940BC + 4 * index, length, 4)]
    for i in range(length):
        settler = rng.randrange(1, 32000)
        fields += [(0x118A47C + index * 200 + 2 * i, settler, 2),
                   (SETTLERS + settler * SETTLER_BYTES + 0xE, 142 if rng.random() < share else rng.randrange(300), 2)]
    return fields


def fighting_percent(rng, pool):
    return rng.randrange(101) if rng.random() < 0.9 else rng.getrandbits(32)


HUNTERS = SCRATCH + 5 * SCRATCH_SLOT_BYTES  # 24 hunter entries {hunter, target}
HUNTER_SLOTS = 24


def hunter_entries(rng, pool, args):
    """24 entries {hunter, target} in scratch 5 (targets often 0, hunters
    sometimes >= 32000); each target stands on a tile of an area 0..7 owned
    by the player, another one or none."""
    player = args[0]
    fields = []
    owners = {}
    for k in range(HUNTER_SLOTS):
        hunter = rng.randrange(32000) if rng.random() < 0.97 else rng.randrange(32000, 40000)
        target = 0 if rng.random() < 0.2 else rng.randrange(1, 32000)
        fields += [(HUNTERS + 8 * k, hunter, 4), (HUNTERS + 8 * k + 4, target, 4)]
        if target:
            x, y, area = rng.randrange(0x300), rng.randrange(0x300), rng.randrange(8)
            owners[area] = 0xFF if rng.random() < 0.1 else rng.choice((player, player, (player + 1) % 20))
            fields += [(SETTLERS + target * SETTLER_BYTES + 4, x, 2), (SETTLERS + target * SETTLER_BYTES + 6, y, 2),
                       (TILES + (x * 0x300 + y) * 24 + 0x14, area, 2)]
    return fields + [(AI_AREAS + area * 0x2E0, owner, 1) for area, owner in owners.items()]


def hunter_vector(rng, pool, args):
    """g_aiMapWorld and the player's vector: begin at the entries, 0..24 of
    them in use."""
    vector = AI_HUNTER_VECTORS + 16 * args[0]
    count = 0 if rng.random() < 0.1 else rng.randint(1, HUNTER_SLOTS)
    return [(AI_MAP_WORLD, WORLD_BASE, 4), (vector, WORLD_BASE + HUNTERS, 4),
            (vector + 4, WORLD_BASE + HUNTERS + 8 * count, 4)]


SPECS += [
    Spec("AI_CountSitesOfType", 0x0059db30, "fastcall", [scratch(0), scratch(2), scratch(1)], presets=sites_of_type,
         globals=ai_world_400, max_cases=600),
    Spec("AI_FirstBuildingInArea", 0x0058b410, "stdcall", [with_noise(below(3)), with_noise(below(4))],
         presets=buildings_in_areas, globals=ai_map_world, max_cases=1000),
    Spec("AI_FirstMilitaryBuildingInArea", 0x0058b380, "stdcall", [with_noise(below(3)), with_noise(below(4))],
         presets=buildings_in_areas, globals=ai_map_world, max_cases=1000),
    Spec("AI_MineSuccessPercent", 0x00573810, "fastcall", [scratch(0)], presets=mine_record, globals=ai_world),
    # AX only: off the map the callees keep the upper half of their caller's
    # EAX (the only caller, 0x571fa0, returns AX of the sum).
    Spec("AI_ResourceAmountOfKindAt", 0x00571f60, "fastcall",
         [with_noise(between(0, 0x2FF)), with_noise(between(0, 0x2FF)), resource_kind], eax_mask=0xFFFF,
         presets=resource_tile, globals=ai_query_map),
    # AX only: the callers compare AX (0x5760d0).
    Spec("AI_ResourceAmountNear", 0x00571fa0, "fastcall",
         [with_noise(between(0, 70)), with_noise(between(0, 70)), resource_kind], eax_mask=0xFFFF,
         presets=resource_ring_tiles, globals=ai_small_map_world, max_cases=600),
    Spec("AI_IsFarmLand", 0x0059a720, "fastcall", [scratch(0), scratch(1), scratch(2)], presets=farm_land,
         globals=farm_land_globals),
    Spec("AI_NextSparseMapOffset", 0x0058a250, "fastcall", [scratch(0), anything, scratch(1), scratch(2)],
         presets=sparse_index, globals=ai_map_world),
    Spec("AI_Army_GroupFightingShareAbove", 0x00593040, "thiscall", [below(20), below(10), fighting_percent],
         presets=fighting_group, globals=ai_map_world, max_cases=1000),
    # The bitset's _Xran (0x596570) throws std::out_of_range; probed, since
    # the thrown object lies in its own frame (a stack address that differs
    # between the two runs). Both versions go on as if it returned.
    Spec("AI_Army_PruneHunters", 0x005934a0, "thiscall", [below(20)], presets=hunter_entries,
         globals=hunter_vector, callees={0x00596570: (0, "thiscall", "probe")}, max_cases=1000),
]


# -- computer players A4, session 52: command senders (ai_commands.cpp) -------
#
# GameSettings::IsHost (0x4318a0) and MessageQueue::Push (0x4309c0) are
# probed: IsHost takes the settings object's critical section (Win32 API
# calls of the lock wrappers) and Push its queue's; the senders' own work is
# building the message. Push is recorded with the queue (ECX), the dword
# count, the signal flag and the pushed bytes. The economy senders build the
# message in an uninitialized stack buffer: the bytes they don't write (the
# `gaps` of each spec, always header byte 3) are the original's stack
# leftovers and 0 in ours (ai_commands.cpp: ExecuteCommands doesn't read
# them for these commands), so the record leaves them out, and it leaves out
# the buffer address (a stack address that differs between the two runs).
# The military senders push from g_aiCommandSender's buffer, recorded with
# its address. AI::CountEnemyBowmenOnOwnLandNear (0x58a610, not
# reimplemented yet; a search over the enemy bowmen lists) is probed for
# SendMoveGroup.

AI_COMMAND_SENDER = 0x007E5DE8   # g_aiCommandSender (battle spells, message at +0xc)
AI_MILITARY_QUEUE = 0x007E37C8   # g_aiMilitaryQueue
AI_GROUP_ORDERS = 0x007E37D0     # g_aiGroupOrders
COMMAND_PLAYER = SCRATCH + 4 * SCRATCH_SLOT_BYTES  # a ComputerPlayer (message_queue at +0x760)


def host_flag(h):
    """IsHost's EAX: the low byte 0 a third of the time."""
    value = h & 0xFFFFFFFF
    return value & 0xFFFFFF00 if h % 3 == 0 else value | 1


def pushed_bytes(gaps, with_address):
    """A Push record: queue, count, signal and the message bytes (count * 4
    of them) without the bytes in `gaps`."""
    def record(machine, ecx, args):
        count = args[1]
        data = bytearray(machine.guest_read(args[0], 4 * min(count, 3)))
        for gap in gaps:
            if gap < len(data):
                data[gap] = 0
        return (count, args[2], bytes(data)) + ((args[0],) if with_address else ())
    return record


def command_callees(gaps=(), with_address=False):
    return {0x004318A0: (0, "thiscall", host_flag),
            0x004309C0: (3, "thiscall", "probe", None, pushed_bytes(gaps, with_address)),
            0x0058A610: (4, "thiscall", lambda h: (h % 3) - 1)}


def command_globals(rng, pool, args):
    return [(AI_PLAYER, WORLD_BASE + COMMAND_PLAYER, 4), (AI_MILITARY_QUEUE, rng.getrandbits(32), 4),
            (AI_MAP_WORLD, WORLD_BASE, 4), (AI_WORLD, WORLD_BASE, 4)]


def work_area_world(rng, pool, args):
    """SetWorkArea's search: the building (scratch 0) with its door in
    areas 0..3 (sometimes 7, which no tile has: the whole search runs), the
    tile (scratch 2 / 3) and the tiles around it in areas 0..3, small map
    offsets first."""
    size = rng.randint(40, 70)
    building = rng.randrange(4000)
    x, y = rng.randrange(size + 3), rng.randrange(size + 3)
    door_x, door_y = rng.randrange(size), rng.randrange(size)
    fields = [(MAP_SIZE, size, 4), (SCRATCH, building, 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(256), 1),
              (SCRATCH + 2 * SCRATCH_SLOT_BYTES, x, 2), (SCRATCH + 3 * SCRATCH_SLOT_BYTES, y, 2),
              (BUILDINGS + building * BUILDING_BYTES + 0xA, door_x, 2),
              (BUILDINGS + building * BUILDING_BYTES + 0xC, door_y, 2),
              (TILES + (door_x * 0x300 + door_y) * 24 + 0x14, 7 if rng.random() < 0.1 else rng.randrange(4), 2)]
    for tx in range(max(0, x - 12), min(size, x + 13)):
        for ty in range(max(0, y - 12), min(size, y + 13)):
            if (tx, ty) != (door_x, door_y):
                fields.append((TILES + (tx * 0x300 + ty) * 24 + 0x14, rng.randrange(4), 2))
    for i in range(300):
        fields += [(0x110A588 + 16 * i, rng.randint(-12, 12) & 0xFFFFFFFF, 4),
                   (0x110A58C + 16 * i, rng.randint(-12, 12) & 0xFFFFFFFF, 4)]
    return fields


def work_area_globals(rng, pool, args):
    return command_globals(rng, pool, args) + [(AI_MAP_SIZES, 70, 2), (AI_MAP_SIZES + 2, 70, 2)]


def move_group_world(rng, pool, args):
    """The group's length (often 0) in the world."""
    index = args[0] * 10 + args[1]
    return [(0x11940BC + 4 * index, 0 if rng.random() < 0.25 else rng.randrange(1, 5), 4)]


def move_group_globals(rng, pool, args):
    """command_globals plus the group's target settler, steal flag, target
    building and defending flag, each often 0."""
    index = args[0] * 10 + args[1]
    pick = lambda bits: 0 if rng.random() < 0.5 else rng.getrandbits(bits) or 1  # noqa: E731
    return command_globals(rng, pool, args) + [
        (AI_GROUP_ORDERS + 0x3E8 + 6 * index + 4, pick(16), 2), (AI_GROUP_ORDERS + 0x320 + index, pick(8), 1),
        (AI_CENSUS + 4 + 2 * index, pick(16), 2), (AI_CENSUS + 0x194 + index, pick(8), 1)]


LONG_GAPS = (3, 9, 10, 11)

SPECS += [
    Spec("AI_SendSetWorkArea", 0x0058e220, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         presets=work_area_world, globals=work_area_globals, callees=command_callees((3, 11)), max_cases=400),
    Spec("AI_SendDestroyBuilding", 0x0058e5c0, "fastcall", [scratch(0), scratch(1)], globals=command_globals,
         callees=command_callees((3, 7))),
    Spec("AI_SendChangeGarrison", 0x0058e630, "fastcall", [anything, anything, anything], globals=command_globals,
         callees=command_callees((3,))),
    Spec("AI_SendPlaceBuilding", 0x0058e6a0, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         globals=command_globals, callees=command_callees((3, 11))),
    Spec("AI_SendChangeToolSettings", 0x0058e740, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    Spec("AI_SendRaiseGoodsPriority", 0x0058e7d0, "fastcall", [scratch(0), scratch(1), scratch(2)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    Spec("AI_SendPromoteSoldiers", 0x0058e850, "fastcall", [scratch(0), scratch(1)], globals=command_globals,
         callees=command_callees((3, 6, 7))),
    Spec("AI_SendClearGoodsSetting", 0x0058e8b0, "fastcall", [scratch(0), scratch(1), scratch(2)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    Spec("AI_SendSetAreaCondition", 0x0058ea30, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    Spec("AI_SendSetBuildingWorking", 0x0058eac0, "fastcall", [scratch(0), scratch(1), scratch(2)],
         globals=command_globals, callees=command_callees((3,))),
    Spec("AI_SendToolSetting10", 0x0058eb40, "fastcall", [scratch(0), scratch(1), scratch(2)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    Spec("AI_SendChangeTransportOrder", 0x0058ebc0, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         globals=command_globals, callees=command_callees(LONG_GAPS)),
    # The military senders (`this` = the world's first bytes as the sender).
    Spec("AI_SendCastSpell", 0x00590990, "thiscall", [anything, below(32000), below(12, 0.1), anything, anything],
         globals=command_globals, callees=command_callees(with_address=True)),
    Spec("AI_SendMoveSettler", 0x00590a30, "thiscall", [anything, anything, anything, anything],
         globals=command_globals, callees=command_callees(with_address=True)),
    Spec("AI_SendPatrolSettler", 0x00590af0, "thiscall", [anything, anything, anything, anything],
         globals=command_globals, callees=command_callees(with_address=True)),
    Spec("AI_QueueChangeGarrison", 0x00590bb0, "thiscall", [anything, anything, anything],
         globals=command_globals, callees=command_callees(with_address=True)),
    Spec("AI_SendConvertCarriers", 0x00590c10, "thiscall", [anything, anything, anything, anything, anything],
         globals=command_globals, callees=command_callees(with_address=True)),
    Spec("AI_SendMoveGroup", 0x00590c80, "thiscall", [below(20), below(10), anything, anything],
         presets=move_group_world, globals=move_group_globals, callees=command_callees(with_address=True)),
]

# -- session 53: the battle-spell casters (ai_battle_spells.cpp) -----------

SPELL_TEST_SETTLERS = 64  # settler ids 1..63 stand on the tiles around the mage
SPELL_SETTLER_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35, 0, 3, 36, 9, 255)
PLAYER_LIST_LENGTHS = 0x11940BC
PLAYER_LISTS = 0x118A47C


def spell_caster_world(rng, pool, args):
    """A small map (size 40..80); the mage (args[1]) inside it; the first
    500 map offsets small (+-12); the tiles around the mage hold settlers
    1..63 (or none) of the players 0..3 (or nobody) and of soldier, bowman
    and other types; the player's unit groups 1..9 (0..5 members, sometimes
    an empty or negative length) of those settlers, with health near a third
    of the soldiers' start health in half the cases."""
    size = rng.randint(40, 80)
    player, mage = args[0], args[1]
    x, y = rng.randrange(size), rng.randrange(size)
    record = SETTLERS + mage * SETTLER_BYTES
    fields = [(MAP_SIZE, size, 4), (record + 4, x, 2), (record + 6, y, 2)]
    for i in range(500):
        fields += [(NEIGHBOUR_OFFSETS + 16 * i, rng.randint(-12, 12) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + 16 * i + 4, rng.randint(-12, 12) & 0xFFFFFFFF, 4)]
    density = rng.choice((0.02, 0.1, 0.3))
    for tx in range(max(0, x - 12), min(size, x + 13)):
        for ty in range(max(0, y - 12), min(size, y + 13)):
            settler = rng.randrange(1, SPELL_TEST_SETTLERS) if rng.random() < density else 0
            fields.append((TILES + (tx * 0x300 + ty) * 24, settler, 2))
    healthy = rng.randint(25, 55) if rng.random() < 0.5 else rng.randrange(256)
    for settler in range(SPELL_TEST_SETTLERS):
        if settler == mage:
            continue
        base = SETTLERS + settler * SETTLER_BYTES
        owner = 0xFF if rng.random() < 0.1 else (player if rng.random() < 0.4 else rng.randrange(4))
        fields += [(base, owner, 1), (base + 1, rng.choice(SPELL_SETTLER_TYPES), 1),
                   (base + 3, healthy if rng.random() < 0.85 else rng.randrange(256), 1),
                   (base + 4, rng.randrange(size), 2), (base + 6, rng.randrange(size), 2)]
    if player < 20:
        for group in range(10):
            index = player * 10 + group
            length = -rng.randrange(3) if rng.random() < 0.1 else rng.randrange(6)
            fields.append((PLAYER_LIST_LENGTHS + 4 * index, length & 0xFFFFFFFF, 4))
            for k in range(max(length, 0)):
                fields.append((PLAYER_LISTS + index * 200 + 2 * k, rng.randrange(SPELL_TEST_SETTLERS), 2))
    return fields


def spell_caster_globals(rng, pool, args):
    return command_globals(rng, pool, args) + [(CAMPAIGN_MISSION, 67 if rng.random() < 0.25 else rng.randrange(80), 4)]


def spell_caster(name, address):
    # mage ids 0..63 and sometimes beyond (then the mage is not on the tiles'
    # list); SendCastSpell runs for real, with IsHost and Push probed.
    return Spec(name, address, "stdcall",
                [below(4), below(SPELL_TEST_SETTLERS + 40), between(-1, 9), between(-1, 6)],
                eax_mask=0, presets=spell_caster_world, globals=spell_caster_globals,
                callees=command_callees(with_address=True), max_cases=600)


SPECS += [
    spell_caster("AI_CastRomanSpell10", 0x005957c0),
    spell_caster("AI_CastRomanSpell11", 0x00595910),
    spell_caster("AI_CastEgyptianSpell12", 0x00595a60),
    spell_caster("AI_CastEgyptianSpell13", 0x00595bb0),
    spell_caster("AI_CastEgyptianSpell15", 0x00595c10),
    spell_caster("AI_CastAsianSpell16", 0x00595d30),
    spell_caster("AI_CastAsianSpell17", 0x00595e80),
    spell_caster("AI_CastAsianSpell14", 0x00595fd0),
    spell_caster("AI_CastAmazonSpell24", 0x005960d0),
    spell_caster("AI_CastAmazonSpell25", 0x00596210),
]

# -- session 53: the enemy counts near a tile (ai_enemy_search.cpp) ---------

ENEMY_SOLDIER_TYPES = (5, 8, 15, 30, 31, 32, 33, 34, 35)


def enemy_lists_world(rng, pool, args):
    """Teams 0..3 for the 20 players; each soldier list (some empty) holds
    up to 6 settlers on a 64 x 64 map whose tiles belong to players 0..5 or
    nobody (only the tiles the settlers stand on are set)."""
    fields = []
    next_id = 1
    for player in range(20):
        record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
        fields.append((record + 0xC, rng.randrange(4), 4))
        for settler_type in ENEMY_SOLDIER_TYPES:
            length = 0 if rng.random() < 0.3 else rng.randint(1, 6)
            ids = list(range(next_id, next_id + length))
            next_id += length
            fields.append((record + 0x20 + 2 * settler_type, ids[0] if ids else 0, 2))
            for k, settler in enumerate(ids):
                base = SETTLERS + settler * SETTLER_BYTES
                x, y = rng.randrange(64), rng.randrange(64)
                fields += [(base + 4, x, 2), (base + 6, y, 2),
                           (base + 0x10, ids[k + 1] if k + 1 < length else 0, 2),
                           (TILES + (x * 0x300 + y) * 24 + 8, 0xFF if rng.random() < 0.1 else rng.randrange(6), 1)]
    return fields


def enemy_count_spec(name, address):
    near = lambda rng, pool: rng.randrange(64) if rng.random() < 0.9 else rng.getrandbits(32)  # noqa: E731
    return Spec(name, address, "thiscall", [near, near, below(50, 0.1), below(20)],
                presets=enemy_lists_world, globals=lambda rng, pool, args: [(AI_MAP_WORLD, WORLD_BASE, 4)],
                max_cases=600)


SPECS += [
    enemy_count_spec("AI_CountEnemySoldiersNear", 0x0058a2c0),
    enemy_count_spec("AI_CountEnemyBowmenOnTerritoryNear", 0x0058a610),
]

# -- session 53: CastBattleSpells and InitBattleSpellSlots (ai_battle_spells.cpp)

BATTLE_SPELL_CASTERS = (0x005957C0, 0x00595910, 0x00595A60, 0x00595BB0, 0x00595C10,
                        0x00595D30, 0x00595E80, 0x00595FD0, 0x005960D0, 0x00596210)
AI_COMMAND_SENDER = 0x007E5DE8
RACE_SPELLS = 0x00658D64


def battle_world(rng, pool, args):
    """enemy_lists_world (the enemy count's lists), plus the player's mages
    (ids 2000.., 0..5 of them, races 0..3 or sometimes above), its mana and
    spell costs near each other."""
    fields = enemy_lists_world(rng, pool, args)
    player = args[0]
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    mages = [] if rng.random() < 0.1 else [2000 + 20 * k + rng.randrange(20) for k in range(rng.randint(1, 5))]
    fields.append((record + 0x20 + 2 * 36, mages[0] if mages else 0, 2))
    for k, mage in enumerate(mages):
        base = SETTLERS + mage * SETTLER_BYTES
        race = rng.randrange(4, 256) if rng.random() < 0.1 else rng.randrange(4)
        fields += [(base + 2, race, 1), (base + 4, rng.randrange(64), 2), (base + 6, rng.randrange(64), 2),
                   (base + 0x10, mages[k + 1] if k + 1 < len(mages) else 0, 2)]
    cost = lambda: rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(200)  # noqa: E731
    fields.append((record + 0x8C, cost(), 4))
    for slot in range(10):  # spell_mana[0..7], then promotion_cost and soldier_levels[0]
        fields.append((record + 0x94 + 4 * slot, cost(), 4))
    return fields


def battle_globals(rng, pool, args):
    """g_aiMapWorld; the battle-spell slots 0..7 (sometimes 8, 9)."""
    fields = [(AI_MAP_WORLD, WORLD_BASE, 4)]
    for k in range(12):
        fields.append((AI_COMMAND_SENDER + k, rng.randrange(8, 10) if rng.random() < 0.1 else rng.randrange(8), 1))
    return fields


def race_spell_changes(rng, pool, args):
    """A few entries of g_raceSpells changed (other spells, duplicates,
    missing ones)."""
    fields = []
    for _ in range(rng.randrange(8)):
        spell = rng.randrange(10, 26) if rng.random() < 0.5 else rng.randrange(40)
        fields.append((RACE_SPELLS + 4 * rng.randrange(32), spell, 4))
    return fields


SPECS += [
    # The casters are probed (verified on their own, see above): the log
    # shows which caster runs, with ECX and the arguments, in order with the
    # Random draws (CGameData::Random runs) and the enemy count (runs).
    Spec("AI_CastBattleSpells", 0x005953b0, "thiscall", [below(20)], eax_mask=0,
         presets=battle_world, globals=battle_globals,
         callees={address: (4, "thiscall", "probe") for address in BATTLE_SPELL_CASTERS}, max_cases=600),
    # ECX = the world: the sender's 12 bytes are world bytes 0..11.
    Spec("AI_InitBattleSpellSlots", 0x00590870, "thiscall", [], eax_mask=0, globals=race_spell_changes,
         max_cases=400),
]

# -- session 53: grid-cell resource queries (ai_cell_resources.cpp) --------

AI_LAND_EVALUATION = 0x007E3134
CELL_GRID = SCRATCH + 4 * SCRATCH_SLOT_BYTES  # 9 x 9 AiGridCells (0xc4 bytes each; runs over slots 4..7)


def resource_tiles(rng, x0, y0, size, player):
    """Tiles x0..x0+size-1, y0..y0+size-1: owned by the player (low byte)
    in most cases, with trees, stones and water terrain."""
    owned = rng.choice((0.3, 0.7, 0.95))
    fields = []
    for x in range(max(0, x0), x0 + size):
        for y in range(max(0, y0), y0 + size):
            base = TILES + (x * 0x300 + y) * 24
            roll = rng.randrange(25)
            obj = 0x44 + rng.randrange(16) if roll == 0 else 0x73 + rng.randrange(12) if roll == 1 else rng.randrange(256)
            terrain = 0x60 + rng.randrange(16) if rng.random() < 0.04 else rng.randrange(256)
            owner = player & 0xFF if rng.random() < owned else rng.choice((0xFF, (player + 1) & 0xFF))
            fields += [(base + 6, terrain, 1), (base + 7, obj, 1), (base + 8, owner, 1)]
    return fields


def resource_goods(rng, pool):
    return rng.choice((2, 3, 26, 9, 0)) | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def low_word_noise(n):
    return lambda rng, pool: rng.randrange(n) | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def ai_map_size_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randint(40, 72), 2), (AI_MAP_SIZES + 2, rng.randint(40, 72), 2)]


def owners_land_tiles(rng, pool, args):
    cell_x, cell_y = args[0] & 0xFFFF, args[1] & 0xFFFF
    return resource_tiles(rng, cell_x * 8, cell_y * 8, 10, args[2])


def work_cell_tiles(rng, pool, args):
    building = SCRATCH
    work_x, work_y = rng.randrange(72), rng.randrange(72)
    player = rng.randrange(4)
    return [(building, player, 1), (building + 0x30, work_x, 2), (building + 0x32, work_y, 2)] + \
        resource_tiles(rng, work_x // 8 * 8, work_y // 8 * 8, 10, player)


def resource_cell_world(rng, pool, args):
    """The tile (scratch 0 / 1, words), a 9 x 9 grid of cells (width and
    height 0..9 used) with random presence bytes, and the tiles of the
    3 x 3 cells around the tile."""
    x = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(80)
    y = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(80)
    land = SCRATCH + 2 * SCRATCH_SLOT_BYTES
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2), (land + 0x140, rng.randrange(10), 4),
              (land + 0x144, rng.randrange(10), 4), (land + 0x158, WORLD_BASE + CELL_GRID, 4)]
    for cell in range(81):
        base = CELL_GRID + cell * 0xC4
        fields += [(base + 3, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1),
                   (base + 4, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1)]
    if x < 80 and y < 80:
        fields += resource_tiles(rng, (x // 8 - 1) * 8, (y // 8 - 1) * 8, 26, args[2])
    return fields


def resource_cell_globals(rng, pool, args):
    return ai_map_size_globals(rng, pool, args) + [(AI_LAND_EVALUATION, WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES, 4)]


SPECS += [
    Spec("AI_OwnersLandHasResource", 0x00599df0, "fastcall",
         [low_word_noise(9), low_word_noise(9), low_word_noise(3), resource_goods],
         presets=owners_land_tiles, globals=ai_map_size_globals, max_cases=1000),
    Spec("AI_WorkCellHasTrees", 0x0059a380, "fastcall", [scratch(0)], presets=work_cell_tiles,
         globals=ai_map_size_globals, max_cases=600),
    Spec("AI_WorkCellHasStones", 0x0059a8b0, "fastcall", [scratch(0)], presets=work_cell_tiles,
         globals=ai_map_size_globals, max_cases=600),
    Spec("AI_FindResourceCellAround", 0x0059a120, "fastcall",
         [scratch(0), scratch(1), low_word_noise(3), resource_goods],
         presets=resource_cell_world, globals=resource_cell_globals, max_cases=1000),
]

# -- session 53: FindNode, IsForceProduction, HasSiteNoteNear, -------------
# -- HOUSE_GROUND_REQUEST_NOTE::Init ---------------------------------------


def every_area_nodes(rng, pool, args):
    """The node lists of players 0..2 (NODE_LAND's lists; the others empty):
    a node for each area 0..4 in random order plus up to 3 duplicates, each
    with a random force_production (+0x18); the out pointer at scratch 0."""
    fields = []
    last = {}
    for p in range(20):
        head = NODE_HEADS + 8 * p
        fields.append((NODE_LAND + 12 * p + 4, WORLD_BASE + head, 4))
        last[p] = head
    slot = 0
    for p in range(3):
        areas = list(range(5)) + [rng.randrange(5) for _ in range(rng.randrange(4))]
        rng.shuffle(areas)
        for area in areas:
            node = SCRATCH + SCRATCH_SLOT_BYTES + slot * 0x40
            slot += 1
            fields += [(last[p], WORLD_BASE + node, 4), (node + 8, area | (rng.getrandbits(16) << 16), 4),
                       (node + 0x18, rng.getrandbits(32), 4)]
            last[p] = node
    for p in range(20):
        fields.append((last[p], WORLD_BASE + NODE_HEADS + 8 * p, 4))
    return fields


def every_area_globals(rng, pool, args):
    return [(AI_LAND_EVALUATION, WORLD_BASE + NODE_LAND, 4)]


def notes_near_globals(rng, pool, args):
    size = rng.randint(20, 420)
    return [(AI_PLAYER, WORLD_BASE + AI_NOTES_PLAYER, 4), (AI_MAP_SIZES, size, 2),
            (AI_MAP_SIZES + 2, size if rng.random() < 0.5 else rng.randint(20, 420), 2)]


def ground_note_globals(rng, pool, args):
    edge = lambda: rng.getrandbits(16) if rng.random() < 0.1 else rng.randint(1, 500)  # noqa: E731
    return [(AI_MAP_SIZES, edge(), 2), (AI_MAP_SIZES + 2, edge(), 2)]


SPECS += [
    Spec("AI_FindNode", 0x00573be0, "fastcall", [scratch(0), low_word_noise(3), low_word_noise(6)],
         presets=every_area_nodes, globals=every_area_globals, max_cases=600),
    Spec("AI_IsForceProduction", 0x00572fa0, "fastcall", [low_word_noise(3), low_word_noise(5)],
         presets=every_area_nodes, globals=every_area_globals, max_cases=600),
    Spec("AI_HasSiteNoteNear", 0x00571c40, "fastcall",
         [low_word_noise(4), between(0, 420), between(0, 420), low_word_noise(40)],
         presets=site_notes, globals=notes_near_globals, max_cases=1000),
    # ECX = the world: the note is world bytes 0..0x4b. EAX (AI_SiteKindOf's)
    # is not used by the caller.
    Spec("HOUSE_GROUND_REQUEST_NOTE_Init", 0x00575d60, "thiscall",
         [low_word_noise(71), anything, anything, anything], eax_mask=0, globals=ground_note_globals,
         max_cases=600),
]

# -- session 54 (ai-land): the land evaluation's cells (ai_evaland_cells.cpp) --
# ECX = the world: the CEvaLand object is world bytes 0..0x15b (width +0x140,
# height +0x144, tiles_per_cell +0x14c, cursor +0x150/+0x154, grid +0x158 ->
# CELL_GRID, 9 x 9 cells); g_aiWorld = the world too.


def evaland_cell_tiles(rng, x0, y0):
    """The 64 tiles of a cell with a dominant terrain class (so that the sums
    pass the cap of 250), trees, stones (sometimes a rocky block), tile
    flags, resources mostly of one kind and low or high heights."""
    dominant = rng.choice((0x00, 0x10, 0x10, 0x20, 0x20, 0x30, 0x60))
    mixed = rng.random() * 0.6
    kind = rng.randrange(6) << 4
    stone_rolls = 7 if rng.random() < 0.25 else 1
    fields = []
    for x in range(x0, x0 + 8):
        for y in range(y0, y0 + 8):
            base = TILES + (x * 0x300 + y) * 24
            cls = rng.randrange(10) << 4 if rng.random() < mixed else dominant
            terrain = cls | (5 if rng.random() < 0.25 else rng.randrange(16))
            roll = rng.randrange(8)
            obj = 0x73 + rng.randrange(12) if roll < stone_rolls else 0x44 + rng.randrange(16) \
                if roll == stone_rolls else rng.randrange(256)
            flags = rng.randrange(256) if rng.random() < 0.3 else 0
            resources = (kind if rng.random() < 0.75 else rng.randrange(16) << 4) | rng.randrange(16)
            height = rng.randrange(6 if rng.random() < 0.5 else 40)
            fields += [(base + 4, height, 1), (base + 6, terrain, 1), (base + 7, obj, 1), (base + 10, flags, 1),
                       (base + 0x11, resources, 1)]
    return fields


def evaland_object(rng, width, height):
    divisor = 64 if rng.random() < 0.75 else rng.randint(1, 200)
    return [(0x140, width, 4), (0x144, height, 4), (0x14c, divisor, 4), (0x158, WORLD_BASE + CELL_GRID, 4),
            (MAP_SIZE, rng.randint(40, 100), 4)]


def evaluate_cell_world(rng, pool, args):
    cell_x, cell_y = args[0], args[1]
    return evaland_object(rng, rng.randint(cell_x + 1, 9), rng.randint(cell_y + 1, 9)) + \
        evaland_cell_tiles(rng, cell_x * 8, cell_y * 8)


def evaluate_all_world(rng, pool, args):
    width = rng.choice((0, 1, 2, 3)) if rng.random() < 0.9 else rng.randint(4, 9)
    height = rng.choice((0, 1, 2, 3)) if rng.random() < 0.9 else rng.randint(4, 9)
    return evaland_object(rng, width, height)


def cell_centre_owners(rng, width, height):
    """The centre tiles' owners of the grid's cells: owned by player 0..3 with
    a per-case share (none, few, half, most), else 0xff."""
    share = rng.choice((0.0, 0.03, 0.5, 0.9))
    fields = []
    for cx in range(width):
        for cy in range(height):
            owner = rng.randrange(4) if rng.random() < share else 0xFF
            fields.append((TILES + ((cx * 8 + 4) * 0x300 + cy * 8 + 4) * 24 + 8, owner, 1))
    return fields


def update_next_world(rng, pool, args):
    width, height = rng.randint(1, 9), rng.randint(1, 9)
    return evaland_object(rng, width, height) + cell_centre_owners(rng, width, height) + \
        [(0x150, rng.randrange(width), 4), (0x154, rng.randrange(height), 4)]


def around_tile_world(rng, pool, args):
    return evaland_object(rng, rng.randint(0, 9), rng.randint(0, 9))


def evaland_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randint(40, 80), 2),
            (AI_MAP_SIZES + 2, rng.randint(40, 80), 2)]


def tile_word(rng, pool):
    value = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(88)
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


SPECS += [
    # DEAD 0x554439 / 0x55444c: the caps of the tree and stream tile counts
    # (a cell has 64 tiles, the cap is 250).
    Spec("AI_CEvaLand_EvaluateCell", 0x005540f0, "thiscall", [below(9), below(9)],
         presets=evaluate_cell_world, globals=evaland_globals, max_cases=1000,
         exempt_blocks=(0x554439, 0x55444c)),
    Spec("AI_CEvaLand_EvaluateAllCells", 0x005540a0, "thiscall", [],
         presets=evaluate_all_world, globals=evaland_globals, max_cases=300),
    # EAX: left over (the last callee's or the owner test's), the caller
    # CComputerPlayer::Step doesn't use it.
    Spec("AI_CEvaLand_UpdateNextCells", 0x005547a0, "thiscall", [], eax_mask=0,
         presets=update_next_world, globals=evaland_globals, max_cases=400),
    # EAX: left over (depends on the caller's EBP); the three callers
    # overwrite it.
    Spec("AI_CEvaLand_ReevaluateAroundTile", 0x005548b0, "thiscall", [tile_word, tile_word], eax_mask=0,
         presets=around_tile_world, globals=evaland_globals, max_cases=300),
]

# -- session 54 (ai-land): CountResourceInOwnersLand, CountPlayersResourcesAround --


def field_tiles(rng, x0, y0, size, player):
    """resource_tiles plus heights (TileSlope), tile flags, resources and
    grass / swampy grass / stream terrain."""
    fields = resource_tiles(rng, x0, y0, size, player)
    for x in range(max(0, x0), x0 + size):
        for y in range(max(0, y0), y0 + size):
            base = TILES + (x * 0x300 + y) * 24
            roll = rng.randrange(8)
            terrain = 0x10 | rng.randrange(16) if roll < 3 else 0x15 if roll == 3 else \
                0x60 | rng.randrange(16) if roll == 4 else rng.randrange(256)
            flags = rng.randrange(256) if rng.random() < 0.3 else 0
            height = rng.randrange(8 if rng.random() < 0.5 else 30)
            fields += [(base + 4, height, 1), (base + 6, terrain, 1), (base + 10, flags, 1),
                       (base + 0x11, rng.randrange(256), 1)]
    return fields


def count_goods(rng, pool):
    goods = rng.choice((2, 3, 5, 6, 7, 9, 21, 25, 26, 27, 28, 8, 0, 4, 29, 0x102))
    return goods | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def count_resource_tiles(rng, pool, args):
    cell_x, cell_y = args[0] & 0xFFFF, args[1] & 0xFFFF
    return field_tiles(rng, cell_x * 8, cell_y * 8, 10, args[2]) + [(MAP_SIZE, rng.randint(40, 100), 4)]


COUNT_LAND = SCRATCH + 8 * SCRATCH_SLOT_BYTES  # the CEvaLand object (grid at CELL_GRID)


def count_around_world(rng, pool, args):
    """Cell x, y, player and goods words at scratch 0..3; a 9 x 9 grid with
    random presence bytes (width and height 0..9) and the tiles of the 3 x 3
    cells around."""
    x = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(10)
    y = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(10)
    player = rng.randrange(3)
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2), (SCRATCH + 2 * SCRATCH_SLOT_BYTES, player, 2),
              (SCRATCH + 3 * SCRATCH_SLOT_BYTES, count_goods(rng, pool) & 0xFFFF, 2),
              (COUNT_LAND + 0x140, rng.randrange(10), 4), (COUNT_LAND + 0x144, rng.randrange(10), 4),
              (COUNT_LAND + 0x158, WORLD_BASE + CELL_GRID, 4), (MAP_SIZE, rng.randint(40, 100), 4)]
    for cell in range(81):
        base = CELL_GRID + cell * 0xC4
        fields += [(base + k, 0 if rng.random() < 0.33 else rng.randrange(1, 256), 1) for k in range(2, 0xF)]
    if x < 10 and y < 10:
        fields += field_tiles(rng, (x - 1) * 8, (y - 1) * 8, 24, player)
    return fields


def count_around_globals(rng, pool, args):
    return ai_map_size_globals(rng, pool, args) + [(AI_LAND_EVALUATION, WORLD_BASE + COUNT_LAND, 4)]


SPECS += [
    # DEAD 0x599d8c: the empty-loop exit (y * 8 >= y * 8 + 8 can't happen for
    # a 16-bit y).
    Spec("AI_CountResourceInOwnersLand", 0x00599a30, "fastcall",
         [low_word_noise(9), low_word_noise(9), low_word_noise(3), count_goods],
         presets=count_resource_tiles, globals=ai_map_size_globals, max_cases=1000, exempt_blocks=(0x599d8c,)),
    Spec("AI_CountPlayersResourcesAround", 0x00599f50, "fastcall",
         [scratch(0), scratch(1), scratch(2), scratch(3)],
         presets=count_around_world, globals=count_around_globals, max_cases=600),
]

# -- session 54 (ai-land): DiscountExploitedResources -------------------------
BUILDING_HIGH_WATER = 0x1499e60
BUILDING_RECORDS = 0x1499e64


def discount_world(rng, pool, args):
    """1..8 building records of players 0..3 (or free), mostly finished
    resource buildings with their work point often on their row or column;
    the CEvaLand object at COUNT_LAND (9 x 9 grid at CELL_GRID, random sums)
    and the owners of the grid cells' centre tiles."""
    count = rng.randint(1, 8)
    fields = [(BUILDING_HIGH_WATER, count, 4), (COUNT_LAND + 0x140, 9, 4), (COUNT_LAND + 0x144, 9, 4),
              (COUNT_LAND + 0x14c, 64, 4), (COUNT_LAND + 0x158, WORLD_BASE + CELL_GRID, 4),
              (MAP_SIZE, rng.randint(40, 100), 4)]
    types = (2, 3, 5, 7, 8, 9, 25, 29, 37, 4)
    for building in range(count + 2):
        base = BUILDING_RECORDS + building * 0x3a
        x, y = rng.randrange(80), rng.randrange(80)
        roll = rng.randrange(3)
        work_x = x if roll == 0 else rng.randrange(80)
        work_y = y if roll == 1 else rng.randrange(80)
        kind = rng.randrange(64) if rng.random() < 0.1 else rng.choice(types)
        owner = 0xFF if rng.random() < 0.15 else rng.randrange(4)
        buildup = rng.randrange(1, 256) if rng.random() < 0.15 else 0
        fields += [(base, owner, 1), (base + 2, x, 2), (base + 4, y, 2), (base + 6, kind, 1),
                   (base + 0x21, buildup, 1), (base + 0x30, work_x, 2), (base + 0x32, work_y, 2)]
    return fields + cell_centre_owners(rng, 10, 10)


def discount_globals(rng, pool, args):
    return evaland_globals(rng, pool, args) + [(AI_LAND_EVALUATION, WORLD_BASE + COUNT_LAND, 4)]


SPECS += [
    Spec("AI_DiscountExploitedResources", 0x0059bc20, "fastcall", [low_word_noise(4), anything],
         presets=discount_world, globals=discount_globals, max_cases=300),
]

# -- session 54 (ai-land): HOUSE_GROUND_REQUEST_NOTE's site search (ai_ground_request.cpp) --
# ECX = the world: the note is world bytes 0..0x4b. g_aiPlayer points at
# scratch slot 9 (its world field +0x75c = the world), g_aiPlayerSetup at
# slot 10 (+4: the mines' minimum). CheckBuildingSite (verified on its own)
# is probed with values 0..10: on these random tiles it would almost always
# return 0, so the site values would never vary.
AI_PLAYER_SETUP = 0x007e2eb4
GROUND_PLAYER = SCRATCH + 9 * SCRATCH_SLOT_BYTES
GROUND_SETUP = SCRATCH + 10 * SCRATCH_SLOT_BYTES
MAP_OFFSETS = 0x110a588


def ring_offsets(count):
    """The map offset table's first `count` entries (dx, dy, ring), ordered by
    hexagonal ring as the game builds it."""
    entries = []
    ring = 0
    while len(entries) < count:
        for dx in range(-ring, ring + 1):
            for dy in range(-ring, ring + 1):
                d = max(abs(dx), abs(dy)) if (dx >= 0) == (dy >= 0) else abs(dx) + abs(dy)
                if d == ring:
                    entries.append((dx, dy, ring))
        ring += 1
    return entries[:count]


RING_OFFSETS = ring_offsets(401)


def site_value(h):
    return 0 if h % 10 < 3 else 1 + (h >> 8) % 10


GROUND_CALLEES = {0x004d99e0: (5, "thiscall", site_value)}


def ground_note_world(rng, pool, args):
    """The note, the map offsets, the tiles around the place (owned in 4 x 4
    patches by players 0..2 or nobody: the player number is a global, drawn
    separately) and 0..6 buildings, mostly fields."""
    cx, cy = rng.randint(15, 65), rng.randint(15, 65)
    roll = rng.random()
    kind = rng.choice((7, 8, 9, 29, 37)) if roll < 0.3 else \
        rng.choice((7, 8, 9, 29, 37, 14, 24, 36, 15, 2, 3, 5, 1, 25, 51)) if roll < 0.9 else rng.randrange(64)
    barren = rng.random() < 0.15  # no resources at all (a mine finds no place)
    size = rng.randint(70, 80)
    max_radius = rng.randint(1, 8)
    ring = 0 if rng.random() < 0.2 else rng.randint(0, max_radius)
    fields = [(0x1c, kind, 2), (0x1e, cx + rng.randint(-6, 6), 2), (0x20, cy + rng.randint(-6, 6), 2),
              (0x2c, cx, 2), (0x2e, cy, 2), (0x30, size, 2), (0x32, size, 2), (0x3c, ring, 2),
              (0x3e, rng.randrange(6), 2), (0x40, rng.randrange(ring) if ring else 0, 2), (0x42, max_radius, 2),
              (SCRATCH, cx + rng.randint(-3, 3), 2), (SCRATCH + SCRATCH_SLOT_BYTES, cy + rng.randint(-3, 3), 2),
              (GROUND_PLAYER + 0x75c, WORLD_BASE, 4), (GROUND_SETUP + 4, 0 if rng.random() < 0.2 else rng.randrange(60), 2),
              (MAP_SIZE, size, 4)]
    for k, (dx, dy, d) in enumerate(RING_OFFSETS):
        base = MAP_OFFSETS + 16 * k
        if k < 150:
            fields += [(base, dx & 0xFFFFFFFF, 4), (base + 4, dy & 0xFFFFFFFF, 4)]
        if k in (150, 300, 400):
            fields.append((base + 8, d, 4))
    resource = rng.randrange(6) << 4
    patches = {}
    for x in range(cx - 14, cx + 15):
        for y in range(cy - 14, cy + 15):
            base = TILES + (x * 0x300 + y) * 24
            patch = (x // 4, y // 4)
            if patch not in patches:
                patches[patch] = rng.choice((0, 1, 2, 0xFF))
            owner = patches[patch]
            amount = 0 if barren or rng.random() < 0.3 else rng.randrange(16)
            kind_here = resource if rng.random() < 0.8 else rng.randrange(6) << 4
            fields += [(base + 8, owner, 1), (base + 0x11, kind_here | amount, 1)]
    count = rng.randrange(7)
    fields.append((BUILDING_HIGH_WATER, count, 4))
    for building in range(count + 2):
        base = BUILDING_RECORDS + building * 0x3a
        btype = rng.choice((14, 24, 36, 14, 24, 36, 2, 7)) if rng.random() < 0.9 else kind
        owner = rng.choice((0, 1, 2, 0xFF))
        fields += [(base, owner, 1), (base + 2, cx + rng.randint(-20, 20), 2), (base + 4, cy + rng.randint(-20, 20), 2),
                   (base + 6, btype, 1), (base + 0x21, rng.randrange(1, 256) if rng.random() < 0.15 else 0, 1),
                   (base + 0x30, cx + rng.randint(-25, 25), 2), (base + 0x32, cy + rng.randint(-25, 25), 2)]
    return fields


def ground_note_globals(rng, pool, args):
    size = rng.randint(70, 80)
    return [(AI_WORLD, WORLD_BASE, 4), (AI_PLAYER, WORLD_BASE + GROUND_PLAYER, 4),
            (AI_PLAYER_SETUP, WORLD_BASE + GROUND_SETUP, 4), (AI_PLAYER_NUMBER, rng.randrange(3), 1),
            (AI_MAP_SIZES, size, 2), (AI_MAP_SIZES + 2, size, 2)]


SPECS += [
    # AL only: the caller CheckAndImproveSite tests AL; the upper bytes are
    # the callees' leftovers.
    Spec("HOUSE_GROUND_REQUEST_NOTE_CheckSite", 0x005760d0, "thiscall", [scratch(0), scratch(1)], eax_mask=0xFF,
         presets=ground_note_world, globals=ground_note_globals, callees=GROUND_CALLEES, max_cases=600),
    Spec("HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite", 0x00575df0, "thiscall", [],
         presets=ground_note_world, globals=ground_note_globals, callees=GROUND_CALLEES, max_cases=600),
    Spec("HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition", 0x005763a0, "thiscall", [],
         presets=ground_note_world, globals=ground_note_globals, max_cases=600),
]

# -- session 54 (ai-land): HasForeignCellAround, FewUnownedTilesNear, MarkBuildingTypesInLandGrid --


def foreign_cell_world(rng, pool, args):
    """The tile (scratch 0 / 1), the CEvaLand object at COUNT_LAND (9 x 9 grid,
    width / height 0..9, random flags) and the cells' centre owners."""
    x = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(80)
    y = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(80)
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2), (COUNT_LAND + 0x140, rng.randrange(10), 4),
              (COUNT_LAND + 0x144, rng.randrange(10), 4), (COUNT_LAND + 0x158, WORLD_BASE + CELL_GRID, 4)]
    fields += [(CELL_GRID + cell * 0xC4, rng.getrandbits(16), 2) for cell in range(81)]
    return fields + cell_centre_owners(rng, 10, 10)


def player_number_globals(rng, pool, args):
    return discount_globals(rng, pool, args) + [(AI_PLAYER_NUMBER, rng.randrange(4), 1)]


def few_unowned_world(rng, pool, args):
    x = rng.getrandbits(10) if rng.random() < 0.05 else rng.randrange(400)
    y = rng.getrandbits(10) if rng.random() < 0.05 else rng.randrange(400)
    return [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2)]


def mark_types_world(rng, pool, args):
    """1..12 building records (any owner byte below 0x14 or free, types 0..63:
    from 57 the bitset's _Xran throws) on a 10 x 10 grid (GridCellOfTile
    wraps)."""
    count = rng.randint(1, 12)
    fields = [(BUILDING_HIGH_WATER, count, 4), (COUNT_LAND + 0x140, rng.randint(1, 9), 4),
              (COUNT_LAND + 0x144, rng.randint(1, 9), 4), (COUNT_LAND + 0x158, WORLD_BASE + CELL_GRID, 4)]
    for building in range(count + 2):
        base = BUILDING_RECORDS + building * 0x3a
        kind = rng.randrange(64) if rng.random() < 0.05 else rng.randrange(57)
        owner = 0xFF if rng.random() < 0.15 else rng.randrange(20)
        fields += [(base, owner, 1), (base + 2, rng.randrange(80), 2), (base + 4, rng.randrange(80), 2),
                   (base + 6, kind, 1)]
    return fields


SPECS += [
    Spec("AI_HasForeignCellAround", 0x0059c200, "fastcall", [scratch(0), scratch(1)],
         presets=foreign_cell_world, globals=player_number_globals, max_cases=1000),
    Spec("AI_FewUnownedTilesNear", 0x0059c2c0, "fastcall", [scratch(0), scratch(1)],
         presets=few_unowned_world, globals=ai_world, max_cases=600),
    Spec("AI_MarkBuildingTypesInLandGrid", 0x005723a0, "stdcall", [],
         presets=mark_types_world, globals=discount_globals, max_cases=600),
]

# -- session 54 (ai-land): building orders (ai_building_orders.cpp) -----------
# The senders run (verified); MessageQueue::Push and IsHost are probed as for
# the senders' own specs (command_callees, the unwritten bytes left out).
ORDER_NODES = SCRATCH + 11 * SCRATCH_SLOT_BYTES  # AiPlayerData nodes, 0x400 apart
PLAYER_RECORDS = 0x11943dc
PLAYER_BYTES = 0x148


def order_buildings(rng, count, size):
    """Building records 0..count+1: foresters, small towers, apiaries and
    others of players 0..2 (or free), finished or not, doors on the map."""
    fields = [(BUILDING_HIGH_WATER, count, 4)]
    for building in range(count + 2):
        base = BUILDINGS + building * BUILDING_BYTES
        kind = rng.choice((5, 5, 15, 51, 2)) if rng.random() < 0.9 else rng.randrange(64)
        owner = 0xFF if rng.random() < 0.15 else rng.randrange(3)
        fields += [(base, owner, 1), (base + 2, rng.randrange(size), 2), (base + 4, rng.randrange(size), 2),
                   (base + 6, kind, 1), (base + 0x21, rng.randrange(1, 256) if rng.random() < 0.15 else 0, 1),
                   (base + 0xA, rng.randrange(size), 2), (base + 0xC, rng.randrange(size), 2)]
    return fields


def order_tile_areas(rng, size):
    return [(TILES + (x * 0x300 + y) * 24 + 0x14, rng.randrange(4), 2) for x in range(size) for y in range(size)]


def place_order_world(rng, pool, args):
    return [(SCRATCH + k * SCRATCH_SLOT_BYTES, rng.getrandbits(16), 2) for k in range(4)]


def no_tower_world(rng, pool, args):
    return order_buildings(rng, rng.randrange(20), 64)


def clear_tools_world(rng, pool, args):
    count = rng.randrange(30)
    return [(SCRATCH, rng.randrange(count + 2), 2)] + order_buildings(rng, count, 32) + order_tile_areas(rng, 32)


def foresters_world(rng, pool, args):
    """Area (scratch 0, 0..4) and player (scratch 1, 0..2); the players'
    races (Amazons often); the node lists of players 0..2 (a node for some
    areas, with a small woodcutter count at +0x348); the area's log piles
    are lazily random (AreaGoodsAmount); buildings and door tile areas."""
    size = 32
    fields = [(SCRATCH, rng.randrange(5), 2), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(3), 2)]
    fields += [(PLAYER_RECORDS + p * PLAYER_BYTES + 4, 3 if rng.random() < 0.5 else rng.randrange(4), 4)
               for p in range(20)]
    last = {}
    for p in range(20):
        head = NODE_HEADS + 8 * p
        fields.append((NODE_LAND + 12 * p + 4, WORLD_BASE + head, 4))
        last[p] = head
    for p in range(3):
        for area in range(4):
            if rng.random() < 0.25:
                continue
            node = ORDER_NODES + (p * 4 + area) * 0x400
            fields += [(last[p], WORLD_BASE + node, 4), (node + 8, area, 2), (node + 0x348, rng.randrange(6), 2)]
            last[p] = node
    for p in range(20):
        fields.append((last[p], WORLD_BASE + NODE_HEADS + 8 * p, 4))
    # each area's log piles: a short list (a random head would be followed far)
    for area in range(5):
        head = 0
        for k in range(rng.randrange(4)):
            pile = 1 + area * 100 + k
            fields += [(AI_PILES + pile * 14 + 0xc, head, 2), (AI_PILES + pile * 14 + 1, rng.randrange(9), 1)]
            head = pile
        fields.append((AI_AREAS + area * 0x2e0 + 0xa + 3 * 2, head, 2))
    return fields + order_buildings(rng, rng.randrange(30), size) + order_tile_areas(rng, size)


def foresters_globals(rng, pool, args):
    return command_globals(rng, pool, args) + [(AI_LAND_EVALUATION, WORLD_BASE + NODE_LAND, 4),
                                               (AI_MAP_SIZES, 32, 2), (AI_MAP_SIZES + 2, 32, 2)]


SPECS += [
    Spec("AI_OrderPlaceBuilding", 0x0058e200, "fastcall", [scratch(0), scratch(1), scratch(2), scratch(3)],
         presets=place_order_world, globals=command_globals, callees=command_callees((3, 11)), max_cases=300),
    Spec("AI_NoSmallTowerNear", 0x0058ede0, "fastcall", [low_word_noise(64), low_word_noise(64), low_word_noise(3)],
         presets=no_tower_world, globals=ai_world, max_cases=600),
    Spec("AI_ClearToolGoodsSettings", 0x0058e930, "fastcall", [scratch(0)],
         presets=clear_tools_world, globals=command_globals, callees=command_callees(LONG_GAPS), max_cases=300),
    Spec("AI_SetForestersWorking", 0x0059c0f0, "fastcall", [scratch(0), scratch(1)],
         presets=foresters_world, globals=foresters_globals, callees=command_callees((3,)), max_cases=400),
]

# -- session 54 (ai-land): InitAiGlobals, SaveState (ai_state.cpp) -----------
# InitAiGlobals: ECX = a computer player at scratch 0 whose world (+0x75c) is
# the emulated world (random map size and player records). SaveState: ECX =
# a fake CMapFile; WriteChunkData (verified; it writes a file) is probed.


def init_globals_world(rng, pool, args):
    fields = [(SCRATCH + 0x75c, WORLD_BASE, 4), (MAP_SIZE, rng.getrandbits(32) if rng.random() < 0.2 else rng.randint(64, 464), 4)]
    fields += [(PLAYER_RECORDS + p * PLAYER_BYTES, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4)
               for p in range(20)]
    return fields


SPECS += [
    Spec("CComputerPlayer_InitAiGlobals", 0x00551b80, "fastcall", [scratch(0), anything],
         presets=init_globals_world, globals=lambda rng, pool, args: [], max_cases=300),
    # EAX: WriteChunkData's leftover; the caller 0x551220 returns it to the
    # savegame writer's loop, which doesn't use it.
    Spec("AI_SaveState", 0x00596330, "fastcall", [scratch(1), anything], eax_mask=0,
         callees={0x004a66d0: (4, "thiscall", "probe")}, max_cases=100),
]



# -- session 54 (wip/ai-army): unit-group bookkeeping (ai_army_groups.cpp) ----
#
# `this` (ECX) is the world base, so the army object's fields are world bytes
# 0..0x20f8 (home areas +0, mages +0x988, bitset +0xca8, highest group
# +0x1d10, group size +0x1d60, gather positions +0x1db0, offsets +0x20d0).
# DisbandWeakGroups clears bits in the real g_aiGroupOrders (0x7e37d0) and
# calls MoveLastGroupInto on it, so those globals get random values too.
# std::bitset::_Xran (0x596570) is probed (it throws; see AI_Army_PruneHunters).

ARMY_HOME_AREAS = 0x0
ARMY_MAGES = 0x988
ARMY_BITSET = 0xCA8
ARMY_HIGHEST_GROUP = 0x1D10
ARMY_GROUP_SIZE = 0x1D60
AI_CENSUS_TARGETS = 0x007E5EFC   # g_aiCensus +4: target building per player * 10 + group
AI_CENSUS_DEFENDING = 0x007E608C  # g_aiCensus +0x194
AI_ARMY_STATE = 0x007E58C8
XRAN_PROBE = {0x00596570: (0, "thiscall", "probe")}
ARMY_SETTLERS = 3000  # members are mostly drawn from 1..2999


def army_member(rng):
    return rng.randrange(1, ARMY_SETTLERS) if rng.random() < 0.995 else rng.randrange(32000, 33000)


def army_group_size(rng):
    roll = rng.random()
    if roll < 0.1:
        return rng.randint(-60, 200) & 0xFFFFFFFF
    if roll < 0.3:
        return rng.randint(94, 101)
    return rng.randint(0, 30)


def army_lists(rng, players, size_of, lengths_full=False):
    """World unit lists of `players`: lengths around the group size (at most
    100), members from the settler pool (types: melee soldiers, bowmen,
    anything)."""
    fields = []
    for p in players:
        size = size_of(p)
        signed = size - (1 << 32) if size >= 1 << 31 else size
        full = lengths_full and rng.random() < 0.5
        for g in range(10):
            roll = rng.random()
            if full:
                length = min(100, max(0, signed + rng.randrange(3)))
            elif roll < 0.25:
                length = 0
            elif roll < 0.45:
                length = rng.randint(1, 3)
            elif roll < 0.7:
                length = min(100, max(0, signed + rng.randint(-2, 2)))
            else:
                length = rng.randint(0, 100 if rng.random() < 0.3 else 12)
            index = p * 10 + g
            fields.append((PLAYER_LIST_LENGTHS + 4 * index, length, 4))
            for k in range(min(length + 1, 100)):
                fields.append((PLAYER_LISTS + index * 200 + 2 * k, army_member(rng), 2))
    return fields


def army_settler_types(rng):
    """Types of the pool's settlers: mostly bowmen in half of the cases, so
    that groups without a melee soldier occur."""
    bowmen = rng.random() < 0.5
    fields = []
    for settler in range(1, ARMY_SETTLERS):
        if bowmen and rng.random() < 0.9:
            art = 8
        else:
            art = rng.randrange(5, 36) if rng.random() < 0.5 else rng.randrange(48)
        fields.append((SETTLERS + settler * SETTLER_BYTES + 1, art, 1))
    return fields


def army_object(rng, base, players):
    """Army fields of `players` at `base`: group size, highest group, home
    areas 0..2 (so groups pair up), mage counts, a few bitset words."""
    fields = []
    sizes = {}
    for p in players:
        sizes[p] = army_group_size(rng)
        fields += [(base + ARMY_GROUP_SIZE + 4 * p, sizes[p], 4),
                   (base + ARMY_HIGHEST_GROUP + 4 * p, rng.randrange(10), 4)]
        for g in range(10):
            index = p * 10 + g
            mages = rng.randrange(4) if rng.random() < 0.85 else rng.randint(-60, 200) & 0xFFFFFFFF
            fields += [(base + ARMY_HOME_AREAS + 2 * index, rng.randrange(3), 2),
                       (base + ARMY_MAGES + 4 * index, mages, 4)]
    for word in range(ARMY_SETTLERS // 32 + 1):
        fields.append((base + ARMY_BITSET + 4 * word, rng.getrandbits(32) if rng.random() < 0.5 else 0, 4))
    return fields, sizes


def army_census(rng, players):
    """Census target buildings (often 0) and defending flags of `players`
    (globals)."""
    fields = []
    for p in players:
        for g in range(10):
            index = p * 10 + g
            fields += [(AI_CENSUS_TARGETS + 2 * index, 0 if rng.random() < 0.5 else rng.getrandbits(16), 2),
                       (AI_CENSUS_DEFENDING + index, 0 if rng.random() < 0.5 else rng.getrandbits(8), 1)]
    return fields


def army_player_groups(rng, pool, args):
    """The army at the world base and the lists of the player (args[0])."""
    fields, sizes = army_object(rng, 0, (args[0],))
    if len(args) > 1 and rng.random() < 0.4:  # MoveLastGroupInto: the highest group itself
        fields.append((ARMY_HIGHEST_GROUP + 4 * args[0], args[1], 4))
    fields += army_lists(rng, (args[0],), lambda p: sizes[p], lengths_full=True)
    if len(args) > 1:
        bit_word = ARMY_BITSET + 4 * ((args[1] & 0xFFFF) >> 5)
        if rng.random() < 0.3:  # AssignMage / AssignSoldier: already assigned
            fields.append((bit_word, 0xFFFFFFFF, 4))
        elif args[1] >= 32000:  # a bad index (_Xran probed) whose bit is clear
            fields.append((bit_word, 0, 4))
    return fields


def army_player_globals(rng, pool, args):
    return [(AI_MAP_WORLD, WORLD_BASE, 4)] + army_census(rng, (args[0],))


def army_soldier_world(rng, pool, args):
    """army_player_groups plus areas (high-water mark 0..5, sometimes -1)
    owned by the player, others or none, and buildings in them; the
    soldier's position."""
    fields = army_player_groups(rng, pool, args) + buildings_in_areas(rng, pool, args)
    high = 0xFFFFFFFF if rng.random() < 0.05 else rng.randrange(6)
    fields.append((AREA_HIGH_WATER, high, 4))
    for area in range(6):
        owner = 0xFF if rng.random() < 0.15 else rng.choice((args[0], args[0], rng.randrange(3)))
        fields.append((AI_AREAS + area * 0x2E0, owner, 1))
    soldier = args[1] if args[1] < 0x10000 else 0
    record = SETTLERS + soldier * SETTLER_BYTES
    fields += [(record + 4, rng.randrange(400), 2), (record + 6, rng.randrange(400), 2)]
    return fields


def army_disband_world(rng, pool, args):
    """Every player's lists and active flag (0, 1, 2) and the settler pool's
    types; this's group sizes."""
    fields = []
    sizes = {}
    for p in range(20):
        sizes[p] = army_group_size(rng)
        fields += [(ARMY_GROUP_SIZE + 4 * p, sizes[p], 4),
                   (PLAYER_RECORDS + p * PLAYER_RECORD_BYTES, rng.choice((0, 1, 2, 2)), 4)]
    return fields + army_lists(rng, range(20), lambda p: sizes[p]) + army_settler_types(rng)


def army_disband_globals(rng, pool, args):
    """g_aiGroupOrders (whose bitset and MoveLastGroupInto the function uses)
    with random army fields, the census of every player."""
    fields, _ = army_object(rng, AI_GROUP_ORDERS, range(20))
    return [(AI_MAP_WORLD, WORLD_BASE, 4)] + fields + army_census(rng, range(20))


def army_member_arg(rng, pool):
    return rng.randrange(32000, 33000) if rng.random() < 0.05 else army_member(rng)


def army_home_world(rng, pool, args):
    """The group's length 0..3 or more and its first member."""
    index = args[0] * 10 + args[1]
    length = rng.choice((0, 1, 2, rng.randint(3, 100)))
    member = army_member(rng) % 32000
    return [(PLAYER_LIST_LENGTHS + 4 * index, length, 4), (PLAYER_LISTS + index * 200, member, 2)]


SPECS += [
    Spec("AI_Army_Army", 0x00591c10, "thiscall", [], globals=army_hunter_vectors, max_cases=200),
    Spec("AI_ArmyState_ArmyState", 0x00593f40, "thiscall", [], max_cases=200),
    Spec("AI_Army_CountSpareGroups", 0x00591fb0, "thiscall", [below(20)], presets=army_player_groups,
         globals=army_player_globals, max_cases=1000),
    Spec("AI_Army_CountSpareSoldiers", 0x00592100, "thiscall", [below(20)], presets=army_player_groups,
         globals=army_player_globals, max_cases=1000),
    Spec("AI_Army_MoveLastGroupInto", 0x00592240, "thiscall", [below(20), below(10)], presets=army_player_groups,
         globals=army_player_globals, max_cases=1000),
    Spec("AI_Army_AssignMage", 0x005926e0, "thiscall", [below(20), army_member_arg], presets=army_player_groups,
         globals=army_player_globals, callees=XRAN_PROBE, max_cases=1000),
    # 0x592aeb is dead: it runs only if the address this + 0x1d10 is 0 (the
    # original tests an array's address instead of an element).
    Spec("AI_Army_AssignSoldier", 0x00592820, "thiscall", [below(3), army_member_arg], presets=army_soldier_world,
         globals=army_player_globals, callees=XRAN_PROBE, exempt_blocks=(0x592aeb,), max_cases=1000),
    Spec("AI_Army_DisbandWeakGroups", 0x00592bf0, "thiscall", [], presets=army_disband_world,
         globals=army_disband_globals, callees=XRAN_PROBE, max_cases=300),
    Spec("AI_Army_ReturnGroupHome", 0x00592620, "thiscall", [below(20), below(10)], presets=army_home_world,
         globals=army_player_globals,
         callees={0x0058A610: (4, "thiscall", lambda h: (h % 3) - 1 if h % 2 else 0),
                  0x00590C80: (4, "thiscall", "probe")},
         max_cases=600),
]


# -- session 54 (wip/ai-army): military orders (ai_army_orders.cpp) ---------
#
# AI::CountEnemySoldiersNear (0x58a2c0, verified; it walks every other
# player's soldier lists) and the command senders are probed, so that the
# cases reach every branch; the other callees (TileDistance, IsOnMap,
# ClampToMap, FreeTileArea, NextSparseMapOffset, CGameData::Random,
# ResetUnitGroup, CountSpareSoldiers, StartPositionWalkRegion,
# WeakestFrontierMilitaryBuilding) run as original code on both sides.

AI_GROUP_TARGETS = AI_GROUP_ORDERS + 0x3E8   # 6-byte {x, y, unk4} per player * 10 + group
AI_STEAL_FLAGS = AI_GROUP_ORDERS + 0x320
AI_THIEF_CURSORS = AI_GROUP_ORDERS + 0x8E8
AI_ORDER_BITSET = AI_GROUP_ORDERS + 0xCA8
AI_GEOLOGIST_TIMERS = AI_ARMY_STATE + 0x4B4
AI_ENEMY_SEARCH = 0x007E5DE4
OFFSET_TABLE = 0x110A588  # MapOffset {dx, dy, distance, sector}, 16 bytes
ORDER_MAP = 140


def enemy_count(h):
    """CountEnemySoldiersNear's result: 0 half of the time, else 1..12."""
    return 0 if h % 2 else 1 + (h >> 8) % 12


ORDER_CALLEES = {0x0058A2C0: (4, "thiscall", enemy_count),
                 0x00590A30: (4, "thiscall", "probe"),
                 0x00590AF0: (4, "thiscall", "probe"),
                 0x00590C80: (4, "thiscall", "probe")}


def order_globals(rng, pool, args):
    """Both AI world pointers at the world; the census targets and defending
    flags and the group targets / steal flags of players 0..2 (targets
    often naming buildings 1..12)."""
    fields = [(AI_MAP_WORLD, WORLD_BASE, 4), (AI_WORLD, WORLD_BASE, 4)]
    for p in range(3):
        for g in range(10):
            index = p * 10 + g
            target = 0 if rng.random() < 0.3 else rng.randrange(1, 13)
            fields += [(AI_CENSUS_TARGETS + 2 * index, target, 2),
                       (AI_CENSUS_DEFENDING + index, 0 if rng.random() < 0.6 else rng.randrange(1, 256), 1),
                       (AI_STEAL_FLAGS + index, 0 if rng.random() < 0.6 else 1, 1),
                       (AI_GROUP_TARGETS + 6 * index, 0 if rng.random() < 0.6 else rng.randrange(13), 2),
                       (AI_GROUP_TARGETS + 6 * index + 2, 0 if rng.random() < 0.6 else rng.randrange(13), 2)]
    return fields


def area_tile(x, y, area):
    return (TILES + ((x & 0xFFFFFFFF) * 0x300 + (y & 0xFFFFFFFF)) * 24 + 0x14) & 0xFFFFFFFF, area, 2


def military_buildings(rng, pool, args, owner_choices=(0, 1, 2)):
    """A map of 100..139, buildings 1..12 at 20..99 (owners, types 15..19 and
    others, buildup, garrison chains of 0..4 soldiers) and the tiles the
    searches probe around them: door, +-18 / -+9 (Nearest...), the frontier
    probes at the type's radius (offsets 3500 / 4000 / 5000) in the door's
    area or another one; walk regions 0..2."""
    size = rng.randrange(100, 140)
    count = 0xFFFFFFFF if rng.random() < 0.03 else rng.randrange(13)
    fields = [(MAP_SIZE, size, 4), (BUILDING_COUNT, count, 4)]
    radius = {}
    for index, kind in ((3500, 15), (4000, 16), (5000, 18)):
        radius[kind] = rng.randrange(25)
        fields.append((OFFSET_TABLE + 16 * index + 8, radius[kind], 4))
    point_x, point_y = (args[1] & 0xFFFF, args[2] & 0xFFFF) if len(args) > 2 else (0, 0)
    settler = 100
    for b in range(1, 13):
        record = BUILDINGS + b * BUILDING_BYTES
        x, y = rng.randrange(20, 100), rng.randrange(20, 100)
        kind = rng.choice((15, 16, 18, 19, 15, 16, 18, 17, 1))
        area = rng.randrange(3)
        fields += [(record, 0xFF if rng.random() < 0.1 else rng.choice(owner_choices), 1),
                   (record + 6, kind, 1), (record + 0x21, 0 if rng.random() < 0.8 else rng.randrange(1, 256), 1),
                   (record + 2, x, 2), (record + 4, y, 2), (record + 0xA, x + 1, 2), (record + 0xC, y + 1, 2),
                   area_tile(x + 1, y + 1, area),
                   (TILES + ((x + 1) * 0x300 + y + 1) * 24 + 0x10, rng.randrange(3), 1)]
        other = lambda: area if rng.random() < 0.75 else rng.choice((0, 0, area + 1))  # noqa: E731
        for px, py in ((x - 18, y), (x + 18, y), (point_x - 9, y - 18), (point_x + 9, y + 18)):
            fields.append(area_tile(px, py, other()))
        r = radius.get(kind, 0) + 5
        for px, py in ((x - r, y), (x + r, y), (x - r // 2, y - r), (x + r // 2, y + r)):
            fields.append(area_tile(px, py, other()))
        head = 0
        for _ in range(rng.choice((0, 0, 1, 2, 3, 4))):
            fields.append((SETTLERS + settler * SETTLER_BYTES + 0x10, head, 2))
            head = settler
            settler += 1
        fields.append((record + 8, head, 2))
    if len(args) > 2:
        fields.append(area_tile(point_x, point_y, rng.choice((0, 1, 2))))
    return fields


def order_point(rng, pool):
    return rng.randrange(150) if rng.random() < 0.9 else rng.getrandbits(32)


def walk_region_arg(rng, pool):
    return rng.choice((0, 1, 2, 3, 0xFFFFFFFF, 0xFFFFFFFE, 0x80000000, 256))


def frontier_world(rng, pool, args):
    return military_buildings(rng, pool, args)


def assault_world(rng, pool, args):
    """Player 0's buildings, player 1's start position (in the map) and its
    tile's walk region."""
    fields = military_buildings(rng, pool, args, owner_choices=(0, 0, 1))
    x, y = rng.randrange(20, 100), rng.randrange(20, 100)
    fields += [(PLAYER_RECORDS + PLAYER_RECORD_BYTES + 0x10, x, 4), (PLAYER_RECORDS + PLAYER_RECORD_BYTES + 0x14, y, 4),
               (TILES + (x * 0x300 + y) * 24 + 0x10, rng.randrange(3), 1)]
    return fields


def ratio_bits(rng, pool):
    """CheckGroupTargets' float ratio: simple values (exact products), values
    near k / n, NaN, infinities and random bits."""
    roll = rng.random()
    if roll < 0.4:
        value = rng.choice((0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 0.25, 0.1, 1 / 3, 2 / 3))
    elif roll < 0.7:
        value = rng.randrange(1, 40) / rng.randrange(1, 60)
    elif roll < 0.8:
        return rng.choice((0x7FC00000, 0xFFC00000, 0x7F800000, 0xFF800000, 0x80000000))
    elif roll < 0.9:
        value = -rng.random() * 3
    else:
        return rng.getrandbits(32)
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return (bits + rng.choice((0, 0, 1, -1))) & 0xFFFFFFFF


def group_targets_world(rng, pool, args):
    """military_buildings plus the player's group lists: lengths (small, or
    large so that the attackers' sum gets big), first members."""
    fields = military_buildings(rng, pool, args)
    player = args[0]
    for g in range(10):
        index = player * 10 + g
        roll = rng.random()
        length = rng.randrange(12) if roll < 0.7 else rng.randrange(100) if roll < 0.9 else rng.getrandbits(31)
        fields += [(PLAYER_LIST_LENGTHS + 4 * index, length, 4), (PLAYER_LISTS + index * 200, rng.randrange(1, 32000), 2)]
    return fields


def point_settler(rng, pool, args):
    """The settler's position (0..150, sometimes anything) and the map size."""
    record = SETTLERS + (args[0] & 0xFFFF) * SETTLER_BYTES
    coordinate = lambda: rng.randrange(150) if rng.random() < 0.9 else rng.getrandbits(16)  # noqa: E731
    return [(record + 4, coordinate(), 2), (record + 6, coordinate(), 2), (MAP_SIZE, rng.randrange(1, 200), 4)]


def settler_chain(rng, start, length, art, owner):
    """`length` settlers from `start` on, linked by next (+0x10), of type
    `art` and owner `owner`; returns (head, fields)."""
    fields = []
    ids = [start + 3 * k for k in range(length)]
    for k, settler in enumerate(ids):
        record = SETTLERS + settler * SETTLER_BYTES
        fields += [(record + 0x10, ids[k + 1] if k + 1 < len(ids) else 0, 2), (record + 1, art, 1),
                   (record, owner, 1), (record + 4, rng.randrange(150), 2), (record + 6, rng.randrange(150), 2)]
    return (ids[0] if ids else 0), fields


THIEF_CURSOR = 2500  # the first settler of the cursor chain


def thieves_world(rng, pool, args):
    """The player's thief list (0..9 thieves) and a second chain from
    THIEF_CURSOR on (sometimes not thieves or not the player's), where
    thieves_globals may point the cursor."""
    player = args[0]
    head, fields = settler_chain(rng, rng.randrange(1, 1000), rng.choice((0, 1, 3, 6, 7, 9)), 23, player)
    fields += [(PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 0x20 + 23 * 2, head, 2),
               (MAP_SIZE, rng.randrange(50, 200), 4)]
    _, cursor_fields = settler_chain(rng, THIEF_CURSOR, rng.choice((1, 4, 8)), 23 if rng.random() < 0.8 else 22,
                                     player if rng.random() < 0.8 else player + 1)
    return fields + cursor_fields


def thieves_globals(rng, pool, args):
    """The player's thief cursor: 0, the cursor chain or anything."""
    roll = rng.random()
    cursor = 0 if roll < 0.3 else rng.getrandbits(32) if roll < 0.4 else THIEF_CURSOR
    return [(AI_MAP_WORLD, WORLD_BASE, 4), (AI_WORLD, WORLD_BASE, 4), (AI_THIEF_CURSORS + 4 * args[0], cursor, 4)]


def geologists_world(rng, pool, args):
    """The player's geologist list (0..6) on a map of 50..199; the four map
    corners (where clamped far points land) sometimes mountains (32), and
    tiles near the geologists too."""
    player = args[0]
    size = rng.randrange(50, 200)
    start = rng.randrange(1, 900) if rng.random() < 0.9 else rng.randrange(32000, 32900)  # (bad ids: _Xran)
    head, fields = settler_chain(rng, start, rng.choice((0, 1, 2, 4, 6)), 22, player)
    fields += [(PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 0x20 + 22 * 2, head, 2), (MAP_SIZE, size, 4)]
    for x, y in ((0, 0), (0, size), (size, 0), (size, size)):
        fields.append((TILES + (x * 0x300 + y) * 24 + 6, 32 if rng.random() < 0.3 else rng.randrange(32), 1))
    for k in range(rng.randrange(20)):  # small offsets near the start of the searches
        index = rng.choice((500, 5000)) + k
        fields += [(OFFSET_TABLE + 16 * index, rng.randint(-20, 20) & 0xFFFFFFFF, 4),
                   (OFFSET_TABLE + 16 * index + 4, rng.randint(-20, 20) & 0xFFFFFFFF, 4)]
    return fields


def geologists_globals(rng, pool, args):
    """The call counter around 6, the bitset words of the geologists
    (sometimes busy)."""
    fields = [(AI_MAP_WORLD, WORLD_BASE, 4), (AI_WORLD, WORLD_BASE, 4),
              (AI_GEOLOGIST_TIMERS + 4 * args[0], rng.choice((0, 5, 6, 7, 8, 100, 0xFFFFFFFF)), 4)]
    for word in list(range(32)) + list(range(1000, 1030)):
        fields.append((AI_ORDER_BITSET + 4 * word, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4))
    return fields


SPECS += [
    Spec("AI_NearestFreeInteriorMilitaryBuilding", 0x0058aa70, "thiscall", [below(3), order_point, order_point],
         presets=military_buildings, globals=order_globals, max_cases=1500),
    Spec("AI_WeakestFrontierMilitaryBuilding", 0x0058a7e0, "thiscall", [anything, below(3), walk_region_arg],
         presets=frontier_world, globals=order_globals, max_cases=1500),
    Spec("AI_Mission56FinalAssault", 0x00594d40, "thiscall", [], presets=assault_world, globals=order_globals,
         callees=ORDER_CALLEES, max_cases=800),
    # EAX is not used by the caller (0x58f990).
    Spec("AI_Army_CheckGroupTargets", 0x00594a10, "fastcall", [below(3), anything, ratio_bits], eax_mask=0,
         presets=group_targets_world, globals=order_globals, callees=ORDER_CALLEES, max_cases=2000),
    # 0x594fa0, 0x594fbd, 0x594feb, 0x595008 (and the far version's
    # 0x595300, 0x59531d, 0x59534b, 0x595368) are dead: the remainder of a
    # value masked to 16 bits is never negative.
    Spec("AI_RandomPointNearSettler", 0x00594f70, "fastcall", [between(1, 31999), scratch(0), scratch(1)],
         presets=point_settler, globals=order_globals,
         exempt_blocks=(0x594fa0, 0x594fbd, 0x594feb, 0x595008)),
    Spec("AI_RandomPointNearSettlerFar", 0x005952d0, "fastcall", [between(1, 31999), scratch(0), scratch(1)],
         presets=point_settler, globals=order_globals,
         exempt_blocks=(0x595300, 0x59531d, 0x59534b, 0x595368)),
    # EAX is not used by the caller (0x58f990).
    Spec("AI_SendThieves", 0x00594e70, "fastcall", [below(3), anything], eax_mask=0, presets=thieves_world,
         globals=thieves_globals, callees=ORDER_CALLEES, max_cases=1500),
    Spec("AI_SendGeologists", 0x00595050, "fastcall", [below(3), anything], eax_mask=0, presets=geologists_world,
         globals=geologists_globals, callees={**ORDER_CALLEES, **XRAN_PROBE}, max_cases=1000),
]


# -- session 54 (wip/ai-army): economy agent actions (ai_economy_actions.cpp)
#
# The agent is the world base (barracks +0x82, small temples +0x96); the
# senders SendDestroyBuilding / SendPromoteSoldiers run as original code,
# with IsHost and Push probed (command_callees; the messages' unwritten
# stack bytes 3, 6 and 7 left out).

AI_PLAYER_NUMBER = 0x007E2EB8
AI_PLAYER_SETUP = 0x007E2EB4
ACTION_SETUP = SCRATCH + 5 * SCRATCH_SLOT_BYTES  # an AiPlayerSetup (promotion interval +0xc4)


def economy_actions_world(rng, pool, args):
    """Players 0..2: beds (free 0..200 or anything), residences 26..28
    and other buildings (owner, buildup, births), the computer player's step
    and promotion counts, the setup's interval (sometimes right at the
    threshold), the agent's barracks and small temples."""
    fields = [(BUILDING_COUNT, 0xFFFFFFFF if rng.random() < 0.03 else rng.randrange(40), 4)]
    for player in range(3):  # (g_aiPlayerNumber is one of them, economy_actions_globals)
        record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
        population = rng.randrange(300)
        roll = rng.random()
        spare = (rng.randrange(12) if roll < 0.2 else rng.randrange(8, 35) if roll < 0.45 else
                 rng.randrange(28, 110) if roll < 0.7 else rng.randrange(95, 200) if roll < 0.95 else
                 rng.getrandbits(32))
        fields += [(record + 0xC4, population, 4), (record + 0xC8, (population + spare) & 0xFFFFFFFF, 4)]
    for b in range(1, 40):
        base = BUILDINGS + b * BUILDING_BYTES
        fields += [(base, rng.randrange(3), 1),
                   (base + 6, rng.choice((26, 27, 28, 26, 27, 28, 15, 1)), 1),
                   (base + 0x21, 0 if rng.random() < 0.85 else rng.randrange(1, 256), 1),
                   (base + 0x19, 0 if rng.random() < 0.85 else rng.randrange(1, 11), 1)]
    interval = rng.randrange(150) if rng.random() < 0.7 else rng.getrandbits(16)
    steps = (interval * 14 + rng.randrange(2)) if rng.random() < 0.3 else (
        rng.randrange(2500) if rng.random() < 0.8 else rng.getrandbits(32))
    fields += [(ACTION_SETUP + 0xC4, interval, 2), (COMMAND_PLAYER + 0x764, steps & 0xFFFFFFFF, 4),
               (COMMAND_PLAYER + 0x768, rng.randrange(5), 1),
               (0x82, 0 if rng.random() < 0.25 else rng.randrange(1, 4), 2),
               (0x96, 0 if rng.random() < 0.25 else rng.randrange(1, 4), 2)]
    return fields


def economy_actions_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_PLAYER, WORLD_BASE + COMMAND_PLAYER, 4),
            (AI_PLAYER_SETUP, WORLD_BASE + ACTION_SETUP, 4), (AI_PLAYER_NUMBER, rng.randrange(3), 1)]


SPECS += [
    # EAX is not used by the caller (Run 0x567890).
    Spec("T_ECONOMY_AGENT_DemolishSurplusResidence", 0x0056a000, "thiscall", [], eax_mask=0,
         presets=economy_actions_world, globals=economy_actions_globals,
         callees=command_callees(gaps=(3, 6, 7)), max_cases=1500),
    Spec("T_ECONOMY_AGENT_PromoteSoldiers", 0x0056a0f0, "thiscall", [], eax_mask=0,
         presets=economy_actions_world, globals=economy_actions_globals,
         callees=command_callees(gaps=(3, 6, 7)), max_cases=1500),
]


# -- session 54 (wip/ai-army): woodcutter and stonecutter conditions --------
# (ai_economy_conditions.cpp). The per-race slope tables are set by static
# initializers (zero in the image), so every case gets random ones: real-like
# fractions, values whose products land on integer boundaries (1/3, k/n),
# and random bits (NaN, huge: _ftol's "indefinite" 0).

CUTTER_SLOPES = (0x007E1280, 0x007E1300)  # g_aiStonecutterSlopes, g_aiWoodcutterSlopes: float[8][4]


def cutter_slope_bits(rng):
    roll = rng.random()
    if roll < 0.12:
        return rng.getrandbits(32)
    if roll < 0.3:
        value = rng.randint(1, 20) / rng.randint(1, 500)
    elif roll < 0.4:
        value = 1 / 3
    elif roll < 0.45:
        value = -rng.random()
    else:
        value = rng.randrange(1000) / 997.0
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return (bits + rng.choice((0, 0, 0, 1, -1))) & 0xFFFFFFFF


def cutter_globals(rng, pool, args):
    fields = economy_globals(rng, pool, args)
    for table in CUTTER_SLOPES:
        fields += [(table + 4 * k, cutter_slope_bits(rng), 4) for k in range(32)]
    return fields


def cutter_agent(rng, pool, args):
    """economy_agent with races 0..7 (the tables are indexed race * 5 + stage
    without a check, so far-off races fault), sometimes no stonecutter and
    stock around the wanted amounts."""
    fields = [f for f in economy_agent(rng, pool, args) if f[0] != 8]
    fields.append((8, rng.randrange(4) if rng.random() < 0.85 else rng.randrange(4, 8), 4))
    if rng.random() < 0.5:
        fields += [(0x42, rng.randrange(150), 2), (0x44, rng.randrange(150), 2), (0x50, rng.randrange(150), 2),
                   (0x52, rng.randrange(12), 2), (0x54, rng.randrange(12), 2)]
    return fields


SPECS += [
    Spec(name, address, "thiscall", [], eax_mask=0, presets=cutter_agent, globals=cutter_globals, max_cases=2000)
    for name, address in (("T_ECONOMY_AGENT_ConsiderWoodcutter", 0x00567e00),
                          ("T_ECONOMY_AGENT_ConsiderStonecutter", 0x00568420))
]


# -- session 55 (wip/ai-econ): the residences condition 0x568890 -----------
# (ai_economy_conditions.cpp). The agent at world +0, sites of the residence
# types (and others) for AI::CountSitesOfType, the players' surplus carriers,
# random residence slopes (the static initializers' values, rounding edges,
# any bits) and the world's random state (lazily random). sprintf and
# DebugLog (a bare ret) run as original code on both sides.

AI_RESIDENCE_SLOPES = 0x007E1380  # g_aiResidenceSlopes: float[4]
RESIDENCE_SLOPES = (0.2, 0.375, 0.5, 0.5)


def residence_agent(rng, pool, args):
    area = rng.randrange(4)
    race = rng.randrange(4)
    fields = [(8, race, 4), (0xC, area, 2)]
    low, high = rng.choice(((0, 40), (40, 101), (101, 501), (500, 2001), (2000, 2500), (0, 0x10000)))
    population = rng.choice((0, 39, 40, 41, 99, 100, 101, 499, 500, 501, 1999, 2000, 2001)) \
        if rng.random() < 0.25 else rng.randrange(low, high)
    carriers = rng.getrandbits(16) if rng.random() < 0.1 else rng.randrange(600)
    fields += [(0x3E, population, 2), (0x40, carriers, 2)]
    count = 0 if rng.random() < 0.4 else rng.randrange(12)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x, y = rng.randrange(400), rng.randrange(400)
        fields += [(record, 0xFF if rng.random() < 0.15 else rng.randrange(3), 1),
                   (record + 6, rng.choice((26, 27, 28, 24, 1)), 1),
                   (record + 0x21, 0 if rng.random() < 0.4 else rng.randrange(1, 256), 1),
                   (record + 0xA, x, 2), (record + 0xC, y, 2),
                   (TILES + (x * 0x300 + y) * 24 + 0x14, area if rng.random() < 0.6 else rng.randrange(4), 2)]
    for player in range(3):
        surplus = 0 if rng.random() < 0.5 else rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(1, 30)
        fields.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 0xD4, surplus, 4))
    return fields


def residence_globals(rng, pool, args):
    fields = economy_globals(rng, pool, args)
    for s in range(4):
        roll = rng.random()
        if roll < 0.15:
            bits = rng.getrandbits(32)
        else:
            value = 1 / 3 if roll < 0.3 else rng.randint(1, 20) / rng.randint(1, 500) if roll < 0.45 \
                else RESIDENCE_SLOPES[s]
            bits = struct.unpack("<I", struct.pack("<f", value))[0]
        fields.append((AI_RESIDENCE_SLOPES + 4 * s, bits, 4))
    return fields


SPECS += [
    # EAX is not used by the caller (0x56a1a0).
    Spec("T_ECONOMY_AGENT_ConsiderResidences", 0x00568890, "thiscall", [], eax_mask=0, presets=residence_agent,
         globals=residence_globals, max_cases=2000),
]


# -- session 55 (wip/ai-econ): AreaInDirection (ai_map_queries.cpp), ------
# -- FindTrees/StonesNearOwnFields (ai_cell_resources.cpp) ------------------


def direction_world(rng, pool, args):
    """x, y, direction in scratch 0..2 (positions near and past the map's
    edges, directions 0..7 or any word); tile areas mostly 0 around."""
    x = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(64)
    y = rng.getrandbits(16) if rng.random() < 0.05 else rng.randrange(64)
    direction = rng.getrandbits(16) if rng.random() < 0.1 else rng.randrange(8)
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2), (SCRATCH + 2 * SCRATCH_SLOT_BYTES, direction, 2)]
    density = rng.choice((0.02, 0.1, 0.3))
    for tx in range(max(0, x - 11), min(64, x + 12)):
        for ty in range(max(0, y - 11), min(64, y + 12)):
            area = rng.randrange(1, 6) if rng.random() < density else 0
            fields.append((TILES + (tx * 0x300 + ty) * 24 + 0x14, area, 2))
    return fields


FIELD_TYPES = (24, 14, 36, 24, 14, 36, 2)


def own_fields_world(rng, pool, args):
    """x, y (scratch 0 / 1), the land evaluation (scratch 2; grid 1..9 x
    1..9 at CELL_GRID with trees_around / stones_around), up to 12 buildings
    (farms, vineyards, rice farms, woodcutters; owners 0..2 or free;
    finished or not) with door tiles in areas and work points near (x, y),
    and resource tiles around the work points' cells."""
    x, y = rng.randrange(8, 72), rng.randrange(8, 72)
    if rng.random() < 0.05:
        x = rng.getrandbits(16)
    land = SCRATCH + 2 * SCRATCH_SLOT_BYTES
    fields = [(SCRATCH, x, 2), (SCRATCH + SCRATCH_SLOT_BYTES, y, 2), (land + 0x140, rng.randrange(1, 10), 4),
              (land + 0x144, rng.randrange(1, 10), 4), (land + 0x158, WORLD_BASE + CELL_GRID, 4)]
    for cell in range(81):
        base = CELL_GRID + cell * 0xC4
        fields += [(base + 3, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1),
                   (base + 4, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1),
                   (base + 0xF, 0 if rng.random() < 0.4 else rng.randrange(1, 256), 1),
                   (base + 0x10, 0 if rng.random() < 0.4 else rng.choice((0x100, rng.randrange(1, 300))), 2)]
    area = rng.randrange(1, 3)
    if x < 72 and y < 72:
        fields.append((TILES + (x * 0x300 + y) * 24 + 0x14, area if rng.random() < 0.9 else 0, 2))
    count = rng.randrange(12)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        dx, dy = rng.randrange(64), rng.randrange(64)
        wx = min(63, max(0, (x if x < 72 else 30) + rng.randint(-40, 40)))
        wy = min(63, max(0, y + rng.randint(-40, 40)))
        owner = 0xFF if rng.random() < 0.1 else rng.randrange(3)
        fields += [(record, owner, 1), (record + 6, rng.choice(FIELD_TYPES), 1),
                   (record + 0x21, 0 if rng.random() < 0.8 else rng.randrange(1, 256), 1),
                   (record + 0xA, dx, 2), (record + 0xC, dy, 2), (record + 0x30, wx, 2), (record + 0x32, wy, 2),
                   (TILES + (dx * 0x300 + dy) * 24 + 0x14, area if rng.random() < 0.7 else rng.randrange(3), 2)]
        if rng.random() < 0.4:
            fields += resource_tiles(rng, (wx // 8 - 1) * 8, (wy // 8 - 1) * 8, 18, owner & 3)
    return fields


def own_fields_globals(rng, pool, args):
    """The map 40..72 square, the land evaluation in scratch 2, the player
    number 0..2."""
    return resource_cell_globals(rng, pool, args) + [(AI_PLAYER_NUMBER, rng.randrange(3), 1)]


SPECS += [
    # Its caller (MakeBuildOrders 0x56cd90) compares AX; off the map the
    # upper half is the caller's EAX.
    Spec("AI_AreaInDirection", 0x00599760, "fastcall", [scratch(0), scratch(1), scratch(2)], eax_mask=0xFFFF,
         presets=direction_world, globals=ai_small_map, max_cases=2000),
    Spec("AI_FindTreesNearOwnFields", 0x0059a260, "fastcall", [scratch(0), scratch(1)], presets=own_fields_world,
         globals=own_fields_globals, max_cases=1000),
    Spec("AI_FindStonesNearOwnFields", 0x0059a7a0, "fastcall", [scratch(0), scratch(1)], presets=own_fields_world,
         globals=own_fields_globals, max_cases=1000),
]


# -- session 55 (wip/ai-econ): forced production and tools ----------------
# (ai_force_production.cpp). The player word in scratch 0, the area word in
# scratch 1, the flag / goods byte in scratch 2, a request note in scratch
# 5; the land evaluation in scratch 10 with its 20 list heads in scratch 11
# and nodes in scratch 12..17 (one per area of players 0..2, some missing);
# the computer player (message queue) at COMMAND_PLAYER (scratch 4). The
# command senders run as original code with IsHost and Push probed.

FORCE_LAND = SCRATCH + 10 * SCRATCH_SLOT_BYTES
FORCE_HEADS = SCRATCH + 11 * SCRATCH_SLOT_BYTES
FORCE_NOTE = SCRATCH + 5 * SCRATCH_SLOT_BYTES
FORCE_AREAS = 4
FORCE_TYPES = (2, 3, 4, 5, 12, 12, 24, 15, 7, 26, 25, 37)  # tool users, toolsmiths, others


def force_nodes(rng, need=None):
    """The players' node lists over areas 0..3 (in 6 node slots; `need` =
    (player, area) gets a node for sure)."""
    fields = [(FORCE_LAND + 12 * p + 4, WORLD_BASE + FORCE_HEADS + 8 * p, 4) for p in range(20)]
    last = {p: FORCE_HEADS + 8 * p for p in range(20)}
    slot = 12
    pairs = [(p, area) for p in range(3) for area in range(FORCE_AREAS)]
    rng.shuffle(pairs)
    chosen = ([need] if need else []) + [pair for pair in pairs if pair != need and rng.random() < 0.5]
    for p, area in sorted(chosen[:6]):
        node = SCRATCH + slot * SCRATCH_SLOT_BYTES
        slot += 1
        fields += [(last[p], WORLD_BASE + node, 4), (node + 8, area, 2), (node + 0x10, rng.getrandbits(32), 4),
                   (node + 0x18, rng.randrange(2), 4)]
        last[p] = node
    for p in range(20):
        fields.append((last[p], WORLD_BASE + FORCE_HEADS + 8 * p, 4))
    return fields


def force_areas(rng):
    """Per area 0..3: coal and iron piles (around the limits 5 and 3), tool
    piles, idle worker lists and settings1 bytes."""
    fields, base, settler = [], rng.randint(1, 500), rng.randint(1, 500)
    for area in range(FORCE_AREAS):
        record = AI_AREAS + area * 0x2E0
        for goods, most in ((5, 4), (11, 3), (14, 1), (15, 1), (16, 1), (13, 1), (32, 1), (17, 1)):
            if goods in (5, 11) or rng.random() < 0.3:
                head, chain = economy_pile_chain(rng, base, rng.randrange(most + 1), False)
                fields += chain + [(record + 0xA + goods * 2, head, 2)]
                base += 10
            else:
                fields.append((record + 0xA + goods * 2, 0, 2))
        for worker in range(48):  # (the other lists empty: lazily random ones would be endless chains)
            head = 0
            if worker in (0, 3, 4, 6, 10, 11, 16, 17, 19) and rng.random() < 0.3:
                head = settler
                fields.append((SETTLERS + settler * SETTLER_BYTES + 0x10, 0, 2))
                settler += 1
            fields.append((record + 0x12A + worker * 2, head, 2))
        for goods in range(36):
            fields.append((record + 0x18A + goods, 0 if rng.random() < 0.7 else rng.randrange(1, 4), 1))
    return fields


def force_row_areas(rng, x, y):
    """Areas 0..3 for (x, y) and the 17 tiles after it in +x (AI::AreaAlongRow
    walks on while the area is 0; lazily random words would be areas far
    past the 4096 records)."""
    return [(tile(x + k, y) + 0x14, rng.randrange(FORCE_AREAS), 2) for k in range(18)]


def force_buildings(rng):
    """Up to 20 buildings: owner 0..2 or free, types FORCE_TYPES, finished
    or sites with boards missing, workers or not, position and door on a
    60 x 60 map whose tiles are in areas 0..3."""
    count = rng.randrange(20)
    fields = [(BUILDING_COUNT, count, 4)]
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        x, y, dx, dy = (rng.randrange(60) for _ in range(4))
        fields += [(record, 0xFF if rng.random() < 0.1 else rng.randrange(3), 1),
                   (record + 6, rng.choice(FORCE_TYPES), 1),
                   (record + 0x21, 0 if rng.random() < 0.6 else rng.randrange(1, 256), 1),
                   (record + 0x19, 0 if rng.random() < 0.3 else rng.randrange(1, 9), 1),
                   (record + 8, 0 if rng.random() < 0.7 else rng.randrange(1, 500), 2),
                   (record + 2, x, 2), (record + 4, y, 2), (record + 0xA, dx, 2), (record + 0xC, dy, 2),
                   (tile(dx, dy) + 0x14, rng.randrange(FORCE_AREAS), 2)] + force_row_areas(rng, x, y)
    return fields


def force_world(rng, pool, args, need_node=False):
    player, area = rng.randrange(3), rng.randrange(FORCE_AREAS)
    if rng.random() < 0.05 and not need_node:
        player = rng.randrange(3, 20)
    fields = [(SCRATCH, player, 2), (SCRATCH + SCRATCH_SLOT_BYTES, area, 2),
              (SCRATCH + 2 * SCRATCH_SLOT_BYTES, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1)]
    for p in range(20):
        race = rng.randrange(4) if rng.random() < 0.9 else rng.getrandbits(32)
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, race, 4))
    fields += force_nodes(rng, (player, area) if need_node else None)
    fields += force_areas(rng) + force_buildings(rng)
    return fields


def force_ensure_world(rng, pool, args):
    fields = force_world(rng, pool, args)
    goods = rng.choice((5, 11, 13, 14, 15, 16, 17, 32))
    return [f for f in fields if f[0] != SCRATCH + 2 * SCRATCH_SLOT_BYTES] + [(SCRATCH + 2 * SCRATCH_SLOT_BYTES, goods, 1)]


def force_note_world(rng, pool, args):
    fields = force_world(rng, pool, args)
    x, y = rng.randrange(60), rng.randrange(60)
    fields += [(FORCE_NOTE + 0x1C, rng.choice(FORCE_TYPES + (0, 8, 29, 31)), 2),
               (FORCE_NOTE + 0x1E, x, 2), (FORCE_NOTE + 0x20, y, 2)] + force_row_areas(rng, x, y)
    return fields


def force_globals(rng, pool, args):
    return command_globals(rng, pool, args) + [
        (AI_LAND_EVALUATION, WORLD_BASE + FORCE_LAND, 4), (AI_MAP_SIZES, 60, 2), (AI_MAP_SIZES + 2, 60, 2),
        (AI_PLAYER_NUMBER, rng.randrange(3), 1)]


FORCE_CALLEES = command_callees(LONG_GAPS)

SPECS += [
    # EAX is not used by the callers (EnsureGoodsSupply sets 1, the others
    # return without using it).
    Spec("COMP_ForceProduction", 0x00572fc0, "fastcall", [scratch(0), scratch(1), scratch(2)], eax_mask=0,
         presets=force_world, globals=force_globals, callees=FORCE_CALLEES, max_cases=2000),
    Spec("COMP_EmergencyPowersActWood", 0x00573670, "fastcall", [scratch(0), scratch(1), scratch(2)], eax_mask=0,
         presets=force_world, globals=force_globals, callees=FORCE_CALLEES, max_cases=1500),
    Spec("AI_EnsureGoodsSupply", 0x00573760, "fastcall", [scratch(2), scratch(0), scratch(1)],
         presets=force_ensure_world, globals=force_globals, callees=FORCE_CALLEES, max_cases=1500),
    # AI::IsForceProduction faults for an area without a node (as in the
    # game): the player's area always has one. EAX is not used by 0x550f80.
    Spec("AI_OrderMissingTools", 0x0059bf70, "fastcall", [scratch(0), scratch(1)], eax_mask=0,
         presets=lambda rng, pool, args: force_world(rng, pool, args, need_node=True), globals=force_globals,
         callees=FORCE_CALLEES, max_cases=1500),
    Spec("LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool", 0x0057abc0, "thiscall", [scratch(5)],
         presets=force_note_world, globals=force_globals, callees=FORCE_CALLEES, max_cases=1500),
    # DeactivateNoteOf, CheckWorkerOrTool and ReportToRequester probed (their
    # own specs cover them); SetFlags runs on the note.
    Spec("LOCAL_WARE_REQUEST_AGENT_WorkOnNote", 0x0057ab70, "thiscall", [scratch(5)],
         presets=force_note_world, globals=force_globals,
         callees={0x00551610: (1, "thiscall", "probe"), 0x0057abc0: (1, "thiscall", lambda h: h % 2 if h % 3 else h),
                  0x00582480: (2, "thiscall", "probe")}, max_cases=300),
]


# -- session 55 (wip/ai-econ): the request Execute chain and ---------------
# -- IsAllowedBeforeBasicEconomy (ai_request_execute.cpp) ------------------

EARLY_TYPES = (2, 3, 4, 5, 15, 16, 18, 24, 1, 0, 6, 14)


def early_data(rng):
    """g_aiPlayerData at scratch 0: basic_economy mostly 0, the building
    counts small (often 0)."""
    fields = [(SCRATCH + 0xC, rng.getrandbits(32) if rng.random() < 0.15 else 0, 4)]
    fields += [(SCRATCH + 0x344 + 2 * t, 0 if rng.random() < 0.5 else rng.randrange(6), 2) for t in range(57)]
    return fields


def early_globals(rng, pool, args):
    """The player number 0..2 (races 0..3 or any word), the data."""
    fields = [(AI_WORLD, WORLD_BASE, 4), (AI_PLAYER_DATA, WORLD_BASE + SCRATCH, 4),
              (AI_PLAYER_NUMBER, rng.randrange(3), 1)]
    return fields


def early_world(rng, pool, args):
    fields = early_data(rng)
    for p in range(3):
        race = rng.randrange(4) if rng.random() < 0.85 else rng.getrandbits(32)
        fields.append((PLAYER_RECORDS + p * PLAYER_RECORD_BYTES + 4, race, 4))
    return fields


def early_type(rng, pool):
    value = rng.choice(EARLY_TYPES) if rng.random() < 0.9 else rng.randrange(60)
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def place_world(rng, pool, args):
    """The note at world +0 (type +0x1c, spiral position +0x2c / +0x2e on a
    48 x 48 map), tile owners in patches, the row of areas after the
    position, small towers and other buildings near it, the data."""
    fields = early_world(rng, pool, args)
    x, y = rng.randrange(48), rng.randrange(48)
    fields += [(0x1C, rng.choice(EARLY_TYPES), 2), (0x2C, x, 2), (0x2E, y, 2),
               (tile(x, y) + 8, rng.choice((0, 1, 2, 0xFF)) if rng.random() < 0.3 else rng.randrange(3), 1)]
    fields += [(tile(x + k, y) + 0x14, rng.randrange(4), 2) for k in range(18)]
    count = rng.randrange(12)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        bx = min(47, max(0, x + rng.randint(-20, 20)))
        by = min(47, max(0, y + rng.randint(-20, 20)))
        fields += [(record, 0xFF if rng.random() < 0.1 else rng.randrange(3), 1),
                   (record + 6, 15 if rng.random() < 0.6 else rng.choice(EARLY_TYPES), 1),
                   (record + 2, bx, 2), (record + 4, by, 2)]
    return fields


def place_globals(rng, pool, args):
    return early_globals(rng, pool, args) + command_globals(rng, pool, args) + [
        (AI_MAP_SIZES, 48, 2), (AI_MAP_SIZES + 2, 48, 2)]


EXECUTE_PROBES = {0x00576430: (0, "thiscall", "probe"),
                  0x005824D0: (1, "thiscall", lambda h: 0 if h % 3 == 0 else h),
                  0x00582510: (0, "thiscall", lambda h: h & 0xFFFF0000 if h % 3 == 0 else h)}

SPECS += [
    Spec("AI_IsAllowedBeforeBasicEconomy", 0x005721a0, "fastcall", [early_type, anything], presets=early_world,
         globals=early_globals, max_cases=2000),
    # The callers (the Execute chain) test AX only; the upper half is what
    # the failing callee left.
    Spec("HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding", 0x00576430, "thiscall", [], eax_mask=0xFFFF,
         presets=place_world, globals=place_globals, callees=command_callees((3, 11)), max_cases=2000),
    Spec("HOUSE_GROUND_REQUEST_AGENT_Execute", 0x00576610, "thiscall", [anything], callees=EXECUTE_PROBES),
    Spec("HOUSE_REQUEST_AGENT_Execute", 0x005789b0, "thiscall", [anything], callees=EXECUTE_PROBES),
    # Without a child: AX = 0 on top of OutputDebugStringA's EAX (the caller
    # 0x5655c0 tests AX).
    Spec("T_DO_BUILD_HOUSE_AGENT_Execute", 0x005656e0, "thiscall", [anything], eax_mask=0xFFFF,
         callees=EXECUTE_PROBES),
]


# -- session 55 (wip/ai-econ): work areas (ai_work_areas.cpp) --------------

WORK_LAND = SCRATCH + 10 * SCRATCH_SLOT_BYTES
WORK_GRID = SCRATCH + 11 * SCRATCH_SLOT_BYTES   # 8 x 8 AiGridCells (0x3100 bytes: slots 11..14)
WORK_TYPES = (2, 3, 14, 24, 36)


def same_type_world(rng, pool, args):
    """type (scratch 0), player (scratch 1), x / y (scratch 2 / 3) on a 64 x
    64 map with the row of areas after it; up to 15 buildings of the five
    work-area types (sometimes others) with doors in areas 0..3 and work
    points near; random distances of map offsets 150 / 300 / 400."""
    x, y = rng.randrange(64), rng.randrange(64)
    if rng.random() < 0.03:
        x = rng.randrange(64, 0x10000)
    kind = rng.choice(WORK_TYPES) if rng.random() < 0.85 else rng.choice((1, 30, 25, 40))
    fields = [(SCRATCH, kind, 1), (SCRATCH + SCRATCH_SLOT_BYTES, rng.randrange(3), 1),
              (SCRATCH + 2 * SCRATCH_SLOT_BYTES, x, 2), (SCRATCH + 3 * SCRATCH_SLOT_BYTES, y, 2)]
    if x < 64:
        fields += [(tile(x + k, y) + 0x14, rng.randrange(4) if k else rng.randrange(1, 4), 2) for k in range(18)]
    for k in (150, 300, 400):
        fields.append((0x110A588 + 16 * k + 8, rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(40), 4))
    count = rng.randrange(15)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        dx, dy = rng.randrange(64), rng.randrange(64)
        fields += [(record, 0xFF if rng.random() < 0.1 else rng.randrange(3), 1),
                   (record + 6, kind if rng.random() < 0.6 else rng.choice(WORK_TYPES + (30, 1)), 1),
                   (record + 0x21, 0 if rng.random() < 0.85 else 1, 1),
                   (record + 0xA, dx, 2), (record + 0xC, dy, 2),
                   (tile(dx, dy) + 0x14, rng.randrange(4), 2),
                   (record + 0x30, min(63, max(0, (x if x < 64 else 30) + rng.randint(-30, 30))), 2),
                   (record + 0x32, min(63, max(0, y + rng.randint(-30, 30))), 2)]
    return fields


def same_type_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, 64, 2), (AI_MAP_SIZES + 2, 64, 2)]


def waterworks_world(rng, pool, args):
    """The building (scratch 0: owner, position) on a 64 x 64 map whose
    tiles belong in patches to players 0..2 or nobody, some with stream
    terrain; the land evaluation (scratch 10) with an 8 x 8 grid of random
    stream_tiles."""
    owner = rng.randrange(3)
    fields = [(SCRATCH, owner, 1), (SCRATCH + 2, rng.randrange(64), 2), (SCRATCH + 4, rng.randrange(64), 2),
              (WORK_LAND + 0x140, 8, 4), (WORK_LAND + 0x144, 8, 4), (WORK_LAND + 0x158, WORLD_BASE + WORK_GRID, 4)]
    patch = rng.choice((8, 12, 16, 24))
    stream = rng.choice((0.0, 0.002, 0.02, 0.1, 0.3))
    for x in range(64):
        for y in range(64):
            k = (x // patch) * 7 + (y // patch) * 3
            fields += [(tile(x, y) + 8, 0xFF if k % 4 == 3 else k % 4, 1),
                       (tile(x, y) + 6, 0x60 + rng.randrange(16) if rng.random() < stream else rng.randrange(0x60), 1)]
    for cell in range(64):
        fields.append((WORK_GRID + cell * 0xC4 + 2, 0 if rng.random() < 0.4 else rng.randrange(1, 60), 1))
    return fields


def waterworks_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, 64, 2), (AI_MAP_SIZES + 2, 64, 2),
            (AI_LAND_EVALUATION, WORLD_BASE + WORK_LAND, 4)]


SPECS += [
    # The callers test AL.
    Spec("AI_HasSameTypeWorkAreaNear", 0x0058e390, "fastcall",
         [low_word_noise(16), scratch(0), scratch(1), scratch(2), scratch(3)], eax_mask=0xFF,
         presets=same_type_world, globals=same_type_globals, max_cases=1500),
    Spec("AI_FindWaterworksWorkArea", 0x0059ba90, "fastcall", [scratch(0), scratch(1), scratch(2)],
         presets=waterworks_world, globals=waterworks_globals, max_cases=300),
]


# -- session 55 (wip/ai-econ): small agent slots (ai_agent_slots.cpp) ------
# The agent / note at world +0 (ECX = the world).

SPECS += [
    Spec(name, address, "thiscall", [scratch(0)])
    for name, address in (("T_DO_BUILD_HOUSE_AGENT_GetNoteTypes", 0x005654c0),
                          ("HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes", 0x00576500),
                          ("HOUSE_REQUEST_AGENT_GetNoteTypes", 0x005787b0),
                          ("LOCAL_WARE_REQUEST_AGENT_GetNoteTypes", 0x0057ab60))
] + [
    Spec("HOUSE_GROUND_REQUEST_AGENT_TakeNote", 0x00576510, "thiscall", [scratch(1)],
         globals=lambda rng, pool, args: [(AI_PLAYER, rng.getrandbits(32), 4)],
         callees={0x005515C0: (1, "thiscall", "probe")}),
    Spec("T_ECONOMY_AGENT_vf11", 0x00567870, "thiscall", [anything]),
    # EAX is the caller's (the destructor doesn't touch it); its only caller
    # 0x56fad0 sets EAX = this afterwards.
    Spec("BLACK_BOARD_AGENT_Destructor", 0x0056fac0, "thiscall", [], eax_mask=0),
    Spec("BLACK_BOARD_AGENT_SubclassDeletingDestructor", 0x0056fad0, "thiscall", [anything]),
    Spec("BLACK_BOARD_AGENT_DeletingDestructor", 0x0056faa0, "thiscall", [anything]),
]


# -- session 55 (wip/ai-econ): request agents' slots (ai_request_agents.cpp) --
# Their EAX (slot 9) only reaches the upper halves of slot 4 results, which
# CComputerPlayer::WorkOnNote ignores; WorkOnNote's AX (100) is compared.

AGENT_SETUP = SCRATCH + 2 * SCRATCH_SLOT_BYTES
AGENT_PROBES = {0x005515C0: (1, "thiscall", "probe"), 0x00551610: (1, "thiscall", "probe"),
                0x00582480: (2, "thiscall", "probe"),
                0x00575DF0: (0, "thiscall", lambda h: h % 2 if h % 3 else 0),
                0x005763A0: (0, "thiscall", lambda h: 0 if h % 7 == 0 else h)}


def reported_notes(rng, pool, args):
    """The parent (scratch 0: state, reports) and the child (scratch 1: its
    type, mostly 0 or 1)."""
    return [(SCRATCH + 0x24, rng.randrange(4), 2), (SCRATCH + 0x26, rng.randrange(3), 2),
            (SCRATCH + SCRATCH_SLOT_BYTES + 6, rng.randrange(2) if rng.random() < 0.85 else rng.randrange(6), 2)]


def report_result(rng, pool):
    value = 100 if rng.random() < 0.5 else rng.randrange(3)
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)


def ground_work_world(rng, pool, args):
    """The note (scratch 0: spiral position, skip_check) on a 32 x 32 map of
    players 0..2 in patches; the setup's positions per turn (scratch 2)."""
    fields = [(SCRATCH + 0x2C, rng.randrange(34), 2), (SCRATCH + 0x2E, rng.randrange(32), 2),
              (SCRATCH + 0x44, rng.getrandbits(32) if rng.random() < 0.25 else 0, 4),
              (AGENT_SETUP, 0 if rng.random() < 0.1 else rng.randrange(1, 7), 2)]
    for x in range(32):
        for y in range(32):
            k = x // 8 + y // 8
            fields.append((tile(x, y) + 8, 0xFF if k % 4 == 3 else k % 3, 1))
    return fields


def agent_globals(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, 32, 2), (AI_MAP_SIZES + 2, 32, 2),
            (AI_PLAYER_SETUP, WORLD_BASE + AGENT_SETUP, 4), (AI_PLAYER_NUMBER, rng.randrange(3), 1),
            (AI_PLAYER, rng.getrandbits(32), 4), (AI_PLAYER_DATA, WORLD_BASE + SCRATCH + 3 * SCRATCH_SLOT_BYTES, 4)]


def run_agent(rng, pool, args):
    """The agent's run counter (+8) around the limit; the setup's interval."""
    interval = rng.randrange(8) if rng.random() < 0.9 else rng.getrandbits(16)
    limit = interval * 14 + rng.randrange(3)
    runs = (limit + rng.randint(-2, 1)) & 0xFFFF if rng.random() < 0.6 else rng.getrandbits(16)
    return [(8, runs, 2), (AGENT_SETUP + 0xB2, interval, 2)]


SPECS += [
    Spec("T_DO_BUILD_HOUSE_AGENT_NoteReported", 0x005656a0, "thiscall",
         [scratch(0), scratch(1), report_result, anything], eax_mask=0, presets=reported_notes,
         globals=agent_globals, callees=AGENT_PROBES),
    Spec("HOUSE_REQUEST_AGENT_NoteReported", 0x00578920, "thiscall",
         [scratch(0), scratch(1), report_result, anything], eax_mask=0, presets=reported_notes,
         globals=agent_globals, callees=AGENT_PROBES),
    Spec("HOUSE_GROUND_REQUEST_AGENT_WorkOnNote", 0x00576530, "thiscall", [scratch(0)], eax_mask=0xFFFF,
         presets=ground_work_world, globals=agent_globals, callees=AGENT_PROBES, max_cases=1000),
    Spec("COMPUTER_PLAYER_AGENT_Run", 0x005846a0, "thiscall", [], presets=run_agent, globals=agent_globals),
]


# TakeNote of the build agent and the house request agent: the agent at
# world +0, the note in scratch 0; the new notes' Inits run (verified),
# AddNoteUnder is probed; new_fails reaches Win32::ThrowOutOfMemory (only
# the notes' sizes fail, so its own allocations work).

def take_note_world(rng, pool, args):
    return [(SCRATCH + 0x1C, rng.choice((15, 2, 24, 3, rng.randrange(60))), 2), (SCRATCH + 0x1E, rng.randrange(400), 2),
            (SCRATCH + 0x20, rng.randrange(400), 2), (SCRATCH + 0x22, rng.randrange(40), 2)]


TAKE_NOTE_CALLEES = {0x00551230: (3, "thiscall", "probe")}

SPECS += [
    Spec("T_DO_BUILD_HOUSE_AGENT_TakeNote", 0x005654d0, "thiscall", [scratch(0)],
         presets=take_note_world, globals=ai_world_400, callees=TAKE_NOTE_CALLEES, new_fails=0.1,
         new_fails_size=0x2C, max_cases=500),
    Spec("HOUSE_REQUEST_AGENT_TakeNote", 0x005787c0, "thiscall", [scratch(0)],
         presets=take_note_world, globals=ai_world_400, callees=TAKE_NOTE_CALLEES, new_fails=0.2,
         new_fails_size=(0x4C, 0x24), max_cases=500),
]
