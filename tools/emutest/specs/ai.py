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
