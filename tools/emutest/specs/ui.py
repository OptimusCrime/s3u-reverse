"""Emulator specs, part `ui`: specs of the replacements in src/s3re/game/ui/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""
UI_GROUP = UI_BASE + 0x300
UI_ENABLE = UI_BASE + 0x400
UI_MESSAGE = WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES


def message_type(rng, pool) -> int:
    return rng.choice(MESSAGE_TYPES) | (rng.getrandbits(24) << 8)


def message_position(rng, pool) -> int:
    return (small_coordinate(rng) & 0xFFFF) | ((small_coordinate(rng) & 0xFFFF) << 16)


MESSAGE = [message_type, anything, message_position, anything, anything]
small = lambda rng, pool: rng.choice([0, 1, 2]) if rng.random() < 0.5 else rng.getrandbits(32)  # noqa: E731


def dialog_globals(rng, pool, args):
    return [(0x007a8020 + 4 * i, rng.getrandbits(32), 4) for i in range(8)] + \
           [(address, rng.getrandbits(32), 4) for address in (0x007a8238, 0x007a823c, 0x007a8240)]

SPECS += [
    ui_spec("CScreenElement_Construct", 0x00415610, []),
    ui_spec("CScreenElement_ConstructRect", 0x00415660, [one_of(UI_RECTS, ELEMENT + 8)]),
    ui_spec("CScreenElement_Destroy", 0x004156b0, []),
    ui_spec("CScreenElement_DeletingDestroy", 0x00415640, [string_bool]),
    ui_spec("CScreenElement_Rect", 0x004156d0, []),
    ui_spec("CScreenElement_SetRect", 0x004156e0, [one_of(UI_RECTS, UI_RECTS, ELEMENT + 8)]),
    ui_spec("CScreenElement_MoveBy", 0x00415780, [small, small]),
    ui_spec("CScreenElement_SetParent", 0x004157d0, [one_of(0, UI_PARENT, UI_PARENT, 0x1234)]),
    ui_spec("CScreenElement_ContainsPoint", 0x00415800, [message_position, message_position]),
    ui_spec("CScreenElement_NeedsRedraw", 0x00415830, [], eax_mask=0xFF),
    ui_spec("CScreenElement_IsOffScreen", 0x00415840, []),
    ui_spec("CScreenElement_OnMessage", 0x00415850, MESSAGE),
    ui_spec("CScreenElement_WantsMessage", 0x00415890, [one_of(UI_MESSAGE)], eax_mask=0xFF),
    # The jump table's data (0x415a22..0x415a93) follows the last ret.
    ui_spec("CScreenElement_DispatchMessage", 0x004158d0, MESSAGE, cover_until=0x00415a22),
    ui_spec("CScreenElement_DrawClipped", 0x00415aa0, [anything, one_of(UI_RECTS + 16)]),
    ui_spec("CScreenElement_DrawRect", 0x00415af0, [anything, one_of(UI_RECTS, UI_RECTS + 16), anything]),
    ui_spec("CScreenElement_IntersectRects", 0x00415b20,
            [one_of(UI_BASE + 0x40), one_of(UI_RECTS), one_of(UI_RECTS + 16)]),
    ui_spec("CScreenElement_SetFocus", 0x00415ba0, [anything]),
    ui_spec("CScreenElement_KillFocus", 0x00415bd0, [], eax_mask=0xFF),
    ui_spec("CScreenElement_Invalidate", 0x00415bf0, []),
    ui_spec("CScreenElement_TrackMouse", 0x00415c10, MESSAGE),
    ui_spec("CScreenElement_OnParentChanged", 0x00415ca0, [small]),
    ui_spec("CScreenElement_GetScreen", 0x004160d0, []),
    ui_spec("CScreenElement_SetFlag1F", 0x00416c60, [anything], eax_mask=0),
    ui_spec("UiGroup_WantsMessage", 0x00417100, [one_of(UI_MESSAGE)], this=UI_GROUP, eax_mask=0xFF),
    ui_spec("UiGroup_SetFocus", 0x00417650, [one_of(0, UI_CHILD, UI_CHILD)], this=UI_GROUP),
    ui_spec("UiGroup_KillFocus", 0x00417680, [], this=UI_GROUP, eax_mask=0xFF),
    ui_spec("UiGroup_SetFocus_429d70", 0x00429d70, [one_of(0, UI_CHILD, UI_CHILD)], this=UI_GROUP),
    ui_spec("UiGroup_WantsMessage_429df0", 0x00429df0, [one_of(UI_MESSAGE)], this=UI_GROUP,
            eax_mask=0xFF),
    ui_spec("UiGroup_OnParentChanged_429fd0", 0x00429fd0, [small], this=UI_ENABLE),
    Spec("Dialog_Surface", 0x00416870, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_GraphicsData", 0x004168c0, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_Pen", 0x00416c50, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_PixelFormat", 0x00416880, "stdcall", [scratch(0)], globals=dialog_globals),
    Spec("Dialog_Font", 0x004168a0, "stdcall", [below(14)]),
    Spec("Dialog_Color", 0x004168b0, "stdcall", [below(14)]),
]


# -- batch 24: CScreenGroup, CControl, CGroupControl (screen_element.cpp) ---
#
# On top of ui_objects: the group at UI_GROUP has its children vector at
# -0x74 (vtable = emu.FAKE_VTABLE, data in scratch 2 pointing at four child
# elements at UI_BASE + 0x800 + 0x40 * k), the captured child at -0x50, the
# "use the first rectangle" byte at -0x78. Child calls (virtual_effects)
# sometimes shorten the vector or make it huge (length 0x80000001: then
# every index is out of range, and data[0x80000000] wraps around to
# data[0]); a huge vector is always emptied by the next child call, which
# ends the loop. CControl: its CScreenElement part ("this") at UI_BASE +
# 0x600 (control at -0x3c, vbtable at -0x38 -> the element), and for the
# CGroupControl wrappers at UI_GROUP - 0x7c (vbtable at UI_GROUP - 0xb4).
#
# Exempt blocks (EMPTY): the inlined operator[] check's "vector is empty"
# branch (with the grow-to-one code after it) right after a loop's `index <
# length` check, with no call in between that could change the vector, is
# dead code. The same code is covered in PtrVector_Index and in
# CScreenGroup_HandleMessage's second check (after the 0x20 call).

# NEG: the "index < 0" branch of the same check is dead too (the loop index
# starts at 0 and would need 2^31 iterations to turn negative). LEN0: the
# clamp of the length to 1 after growing an empty vector (the length is 0
# there).
EMPTY_417BE0 = (0x417c2b, 0x417c40, 0x417c48, 0x417c4d, 0x417c5d, 0x417c68, 0x417c6e, 0x417c70, 0x417c74,
                0x417c7c, 0x417c84, 0x417ca5, 0x417cb8, 0x417cbd, 0x417cd1)
EMPTY_417AB0 = (0x417b00, 0x417b15, 0x417b1d, 0x417b22, 0x417b32, 0x417b3d, 0x417b43, 0x417b45, 0x417b49,
                0x417b51, 0x417b59, 0x417b7a, 0x417b8d, 0x417b92, 0x417ba6)
EMPTY_417480 = (0x4174d2, 0x4174ec, 0x4174f8, 0x4174fd, 0x417500, 0x41750c, 0x417514,
                0x417545, 0x41755f, 0x41756b, 0x417570, 0x417573, 0x41757f, 0x417587,
                0x4175c4, 0x4175de, 0x4175ea, 0x4175ef, 0x4175f2, 0x4175fe, 0x417606)
EMPTY_417130 = (0x417224, 0x417239, 0x417246, 0x41724a, 0x417263,  # the 0x16 loop
                0x4172cf, 0x4172e4, 0x4172f5, 0x4172fa, 0x4172fd, 0x41730d, 0x417315,  # first check
                0x4173d0, 0x4173eb)  # second check: LEN0, NEG
EMPTY_4176B0 = (0x417706, 0x41771b, 0x417723, 0x417728, 0x417738, 0x417743, 0x417749, 0x41774b, 0x41774f,
                0x417757, 0x41775f, 0x417780, 0x417793, 0x417798, 0x4177ac)

GROUP_DATA = SCRATCH + 2 * SCRATCH_SLOT_BYTES
CHILDREN = [UI_BASE + 0x800 + 0x40 * k for k in range(4)]
CONTROL = UI_BASE + 0x600
CONTROL_VBTABLE = UI_VBTABLES + 24
GROUP_CONTROL_VBTABLE = UI_VBTABLES + 32
GROUP_VIRTUALS = dict(UI_VIRTUALS)
GROUP_VIRTUALS.update({0x00: 1, 0x18: 1})


def shrink_children(machine, ecx, args, h):
    """A child call that changes the group's vector length (see above)."""
    address = UI_GROUP - 0x60
    length = int.from_bytes(machine.guest_read(address, 4), "little")
    choice = h & 7
    if length >= 0x80000000 or choice == 0:
        value = 0
    elif choice == 1 and not machine.probe_calls[-1][1] in (0x5c, 0x60):
        # (not from TrackMouse's calls: later children may not call at all)
        value = 0x80000001
    elif choice == 2:
        value = (h >> 8) % 3
    else:
        return
    machine.guest_write(address, value.to_bytes(4, "little"))


GROUP_EFFECTS = {slot: shrink_children for slot in (0x18, 0x20, 0x24, 0x28, 0x5c, 0x60, 0x70, 0x74)}


def group_objects(rng, pool, args):
    fields = ui_objects(rng, pool, args)
    group = UI_GROUP - WORLD_BASE
    count = rng.choice([0, 1, 2, 3, 4, 4, 0x80000001])
    fields += [(group - 0x74, FAKE_VTABLE, 4), (group - 0x6c, WORLD_BASE + GROUP_DATA, 4),
               (group - 0x68, rng.choice([0, 1, 4, rng.getrandbits(32)]), 4),
               (group - 0x64, rng.randint(-1, 4) & 0xFFFFFFFF, 4), (group - 0x60, count, 4),
               (group - 0x50, 0 if rng.random() < 0.6 else rng.choice(CHILDREN), 4),
               (group - 0x78, rng.randrange(2), 1)]
    for k, child in enumerate(CHILDREN):
        offset = child - WORLD_BASE
        fields.append((GROUP_DATA + 4 * k, child, 4))
        fields += [(offset, FAKE_VTABLE, 4), (offset + 0x18, rng.choice([0, UI_PARENT]), 4),
                   (offset + 0x1c, rng.randrange(2), 1)]
        if count == 0x80000001 and k == 0:
            # The first child sees the mouse arrive (TrackMouse calls 0x5c),
            # so the huge vector is emptied by an effect in every loop.
            fields.append((offset + 0x1d, 0, 1))
            for i, v in enumerate((0, 0, 0x10000, 0x10000)):
                fields.append((offset + 8 + 4 * i, v & 0xFFFFFFFF, 4))
        else:
            fields.append((offset + 0x1d, rng.randrange(3), 1))
            for i in range(4):
                fields.append((offset + 8 + 4 * i, small_coordinate(rng), 4))
    # The CControl parts: vbtable, tooltip, where the tooltip was opened.
    for this, vbtable in ((CONTROL, CONTROL_VBTABLE), (UI_GROUP - 0x7c, GROUP_CONTROL_VBTABLE)):
        t = this - WORLD_BASE
        fields += [(t - 0x38, WORLD_BASE + vbtable, 4), (vbtable + 4, (ELEMENT - (this - 0x38)) & 0xFFFFFFFF, 4),
                   (t - 0x1c, 0 if rng.random() < 0.3 else UI_CHILD, 4)]
        for field in (-0xc, -0x8):
            value = rng.randint(-40, 120) if rng.random() < 0.9 else rng.getrandbits(32)
            fields.append((t + field, value & 0xFFFFFFFF, 4))
    # A non-zero move offset at scratch 0 + 0x40 (MoveBy calls 0x70 then).
    fields += [(SCRATCH + 0x40, rng.choice([1, -1, 5]) & 0xFFFFFFFF, 4),
               (SCRATCH + 0x44, rng.choice([0, 2, -3]) & 0xFFFFFFFF, 4)]
    return fields


def move_delta(rng, pool) -> int:
    return WORLD_BASE + SCRATCH + 0x40


def group_spec(name, address, args, this=UI_GROUP, **kwargs):
    return Spec(name, address, "fastcall", [one_of(this), anything, *args], presets=group_objects,
                virtuals=GROUP_VIRTUALS, virtual_effects=GROUP_EFFECTS, **kwargs)


DRAW_ARGS = [anything, one_of(UI_RECTS), one_of(UI_RECTS + 16)]

SPECS += [
    group_spec("CScreenGroup_SetFlag1F", 0x00417be0, [anything], exempt_blocks=EMPTY_417BE0),
    group_spec("CScreenGroup_OnMoved", 0x00417ab0, [one_of(UI_RECTS), move_delta], exempt_blocks=EMPTY_417AB0),
    group_spec("CScreenGroup_Draw", 0x00417480, DRAW_ARGS, exempt_blocks=EMPTY_417480),
    # AL is undefined in the original when the captured child declines the
    # message (an uninitialized stack byte), so EAX is not compared here; the
    # in-process test compares AL on every other path.
    group_spec("CScreenGroup_HandleMessage", 0x00417130, MESSAGE, eax_mask=0, exempt_blocks=EMPTY_417130,
               new_fails=0.1),
    group_spec("CScreenGroup_OnMouseMove", 0x004176b0, MESSAGE, exempt_blocks=EMPTY_4176B0),
    group_spec("CScreenGroup_SetFlag1F_429dd0", 0x00429dd0, [anything]),
    group_spec("CScreenGroup_OnMoved_42a030", 0x0042a030, [one_of(UI_RECTS), move_delta]),
    group_spec("CScreenGroup_Draw_42a180", 0x0042a180, DRAW_ARGS),
    group_spec("CScreenGroup_HandleMessage_429e10", 0x00429e10, MESSAGE, eax_mask=0),
    group_spec("CScreen_HandleMessage", 0x00417f60, MESSAGE, this=UI_GROUP + 0x18, eax_mask=0),
    # CControl (EAX: only AL is defined for the message handlers' results,
    # but ours reproduce the whole register where it is known).
    group_spec("CControl_DeleteTooltip", 0x004187b0, [], this=CONTROL - 0x3c, eax_mask=0),
    group_spec("CControl_OnLButtonDown", 0x00418380, MESSAGE, this=CONTROL),
    group_spec("CControl_OnMouseMove", 0x00418430, MESSAGE, this=CONTROL, eax_mask=0xFF),
    group_spec("CControl_OnMouseEnter", 0x004184b0, MESSAGE, this=CONTROL),
    group_spec("CControl_OnMouseLeave", 0x004184e0, MESSAGE, this=CONTROL),
    group_spec("CGroupControl_OnLButtonDown", 0x00429e80, MESSAGE),
    group_spec("CGroupControl_OnMouseMove", 0x00429ec0, MESSAGE, eax_mask=0xFF),
    group_spec("CGroupControl_OnMouseEnter", 0x00429f50, MESSAGE),
    group_spec("CGroupControl_OnMouseLeave", 0x00429f90, MESSAGE),
]

SPECS += [
    ui_spec("CStaticControl_Construct", 0x0041c3c0, [one_of(UI_RECTS, ELEMENT + 8)]),
]

# -- batch 25: background fill and "enabled changed" (screen_element.cpp) ---
#
# The fill (vf11) gets a surface: the world as a CSurface (surface_objects
# above, in the same world as the UI objects at UI_BASE). DDBlt (ours) runs
# inside; its Blt probe records the two DDBLTFX fields that are set (dwSize,
# dwFillColor at +0x50); the original leaves the rest uninitialized.

FILL_VIRTUALS = dict(UI_VIRTUALS)
FILL_VIRTUALS[0x14] = (6, {5: ((0, 4), (0x50, 4))}, {"com": True, "returns": hresult_from})
FILL_VIRTUALS[0x0c] = 0  # (also the owner's restore slot)


def fill_objects(rng, pool, args):
    return ui_objects(rng, pool, args) + surface_objects(rng, pool, args)


def fill_spec(name, address, this):
    return Spec(name, address, "fastcall", [one_of(this), anything, one_of(WORLD_BASE), one_of(0, UI_RECTS)],
                presets=fill_objects, virtuals=FILL_VIRTUALS, callees=EXCEPTION_CTORS, eax_mask=0xFF)


GROUP_EFFECTS[0x6c] = shrink_children

# Dead inlined-bounds-check blocks of 0x4177f0's loop (EMPTY: the
# "vector is empty" branch and its grow-to-one code, right after the loop's
# index < length check with no call in between; NEG: the index < 0 branch;
# see batch 24 above).
EMPTY_4177F0 = (0x417825, 0x41783a, 0x417842, 0x417847, 0x417857, 0x417862, 0x417868, 0x41786a, 0x41786e,
                0x417876, 0x41787e, 0x41789f, 0x4178b2, 0x4178b7, 0x4178cb)

SPECS += [
    fill_spec("CScreenElement_FillBackground", 0x00415b60, ELEMENT),
    fill_spec("CScreenGroup_FillBackground", 0x00417460, UI_GROUP),
    fill_spec("CScreenGroup_FillBackground_429e50", 0x00429e50, UI_GROUP),
    group_spec("CScreenGroup_OnShow", 0x004177f0, [anything], exempt_blocks=EMPTY_4177F0),
    group_spec("CControl_OnShow", 0x00418520, [anything], this=CONTROL),
    group_spec("CGroupControl_OnShow", 0x0042a000, [anything]),
]


# -- batch 27: CStaticText (ui_static_text.cpp) ------------------------------
#
# The object at world offset 0; its text from string_world's second string
# (scratch slot 0); the rectangle in scratch slot 5 (lazily random). The
# text member's allocator byte is the low byte of the rectangle's address.
SPECS += [
    Spec("CStaticText_Construct", 0x0041c3e0, "thiscall",
         [one_of(WORLD_BASE + SCRATCH + 5 * SCRATCH_SLOT_BYTES, WORLD_BASE + SCRATCH + 5 * SCRATCH_SLOT_BYTES + 0x31),
          one_of(STRING_OTHER_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR, WORLD_BASE + 0x28),
          one_of(0, 0, 0x24, 7, 0x12345678)],
         presets=string_world, globals=null_string_count,  # see String_FromSubstrDefault (0x41c4c6)
         # Dead: 0x41c434 caps a length above 0xFFFFFFFF; 0x41c445, 0x41c460
         # and 0x41c488 are the _Xran, erase-memmove and _Eos of the
         # self-assignment, whose length was just set to 0; 0x41c4f9 copies
         # from the static empty string when the source has no buffer but a
         # non-zero length (an inconsistent string).
         exempt_blocks=(0x0041c434, 0x0041c445, 0x0041c460, 0x0041c488, 0x0041c4f9)),
    Spec("CStaticText_SetStyle", 0x0041ca60, "thiscall", [anything]),
]


# -- game_settings.cpp (session 53) -------------------------------------------------
SPECS += [
    Spec("GameSettings_StartGoods", 0x00412300, "fastcall", [scratch(0)], max_cases=200),
    Spec("GameSettings_NgoTimeout", 0x00437470, "fastcall", [scratch(0)], max_cases=200),
    Spec("GamePlayerSetup_Colour", 0x0049fec0, "fastcall", [scratch(0)], max_cases=200),
]


# -- GameSettings::MapName (game_settings.cpp, session 55) ---------------------
# ECX = the world as the settings object: its map name (+0x2c) any kind of
# string (common.string_fields: empty, consistent, odd, shared counts,
# frozen); the result a scratch string, sometimes the map name itself.
def map_name_settings(rng, pool, args):
    fields, _ = string_fields(rng, 0x2c, STRING_SELF_BUFFER)
    return fields


def name_null_string_count(rng, pool, args):
    """The count byte before the static empty string: a name with a length
    but no buffer is shared from there unless it is 0xfe or more."""
    return [(NULL_STRING_COUNT, rng.choice([0, 0, 1, 0xfe, 0xff]), 1)]


SPECS += [
    # Dead blocks: 0x436a8c (a length above 0xffffffff); when the result is
    # the map name itself, its fields were just cleared, so the length is 0:
    # 0x436a9d (std::_Xran for a position past it), 0x436ab8 and 0x436adf
    # (the erase of a non-empty tail).
    Spec("GameSettings_MapName", 0x00436a60, "thiscall",
         [lambda rng, pool: WORLD_BASE + 0x2c if rng.random() < 0.1 else STRING_RESULT_PTR],
         presets=map_name_settings, globals=name_null_string_count,
         exempt_blocks=(0x436a8c, 0x436a9d, 0x436ab8, 0x436adf)),
]


# -- GameSettings::PlayerSetup / SaveSetup (game_settings_players.cpp, session 55) --
# ECX = the world as the settings object; the player-setup vector at +0x58
# (vtable at emu.FAKE_VTABLE: slot 1 is the length notification), its data
# block at GS_SETUPS. The scoped lock (Win32::CLock ctor 0x60da00 / dtor
# 0x60da20) is probed: the lock object lives in the caller's frame, so ECX
# is not recorded ("stdcall"), only the critical section passed. The
# GamePlayerSetup members the vector code calls stay the original and are
# probed: the copy constructor 0x49eaf0 (records destination and source),
# the destructor 0x49ec80 (ours too, verified on its own) and, in SaveSetup,
# operator= 0x49edf0, whose effect copies the plain fields the saved values
# come from (+0x00..+0x18, +0x54..+0xbc) into the local copy; the local's
# constructor 0x49e8f0 and destructor run (empty strings, the vector at
# +0x38 from operator new).
GS_VECTOR = 0x58
GS_SETUPS = 0x20000          # world offset of the data block (lazily random elements)
GS_SETUP_BYTES = 0xcc
GS_STACK = (0x00200000, 0x00300000)  # emu.STACK_BASE, + STACK_SIZE


def _gs_stack(value):
    return "stack" if GS_STACK[0] <= value < GS_STACK[1] else value


def player_setup_vector(rng, pool, args):
    capacity = rng.choice([0, 0, 1, 2, 4, rng.getrandbits(32)])
    filled = rng.choice([0, 1, 2, -1, rng.randrange(5)])
    length = rng.choice([0, 0, 1, 2, 3, 5, 0x80000000 + rng.randrange(3), rng.getrandbits(32)])
    data = rng.choice([0, WORLD_BASE + GS_SETUPS])
    return [(GS_VECTOR, FAKE_VTABLE, 4), (GS_VECTOR + 8, data, 4), (GS_VECTOR + 0xc, capacity, 4),
            (GS_VECTOR + 0x10, filled & 0xFFFFFFFF, 4), (GS_VECTOR + 0x14, length, 4)]


def setup_index(rng, pool):
    return rng.choice([0, 1, 2, 4, -1, -3, rng.getrandbits(32)]) & 0xFFFFFFFF


GS_LOCK_CALLEES = {
    0x0060da00: (1, "stdcall", "probe"),   # Win32::CLock::CLock(cs)
    0x0060da20: (0, "stdcall", "probe"),   # Win32::CLock::~CLock
}
GS_ELEMENT_CALLEES = {
    0x0049eaf0: (1, "thiscall", "this"),   # GamePlayerSetup copy constructor
    0x0049ec80: (0, "thiscall", "probe"),  # GamePlayerSetup destructor
}


def _gs_assign(m, ecx, args, h):
    """GamePlayerSetup::operator=(from) on the local copy: the plain fields."""
    from unicorn.x86_const import UC_X86_REG_ECX
    to = m.uc.reg_read(UC_X86_REG_ECX)
    for start, end in ((0x00, 0x18), (0x54, 0xbc)):
        m.guest_write(to + start, bytes(m.guest_read(args[0] + start, end - start)))
    return to


# Chunk 0x14's record byte +0xb and the records past the count, and chunk
# 0x1a's +0x22..+0x23, are the original's stack leftovers (ours writes 0):
# the recorded chunk bytes leave exactly those out (the in-process test
# GameSettings_SaveSetup_MatchesOriginal compares the same way, and its
# wrong-implementation check shows a changed record byte is caught).
def _gs_record_chunk(m, ecx, args):
    chunk_type, sub, buffer = args
    size = struct.unpack("<I", m.guest_read(buffer + 4, 4))[0]
    data = bytearray(m.guest_read(buffer, min(size, 0x400)))
    if chunk_type == 0x14:
        count = struct.unpack("<I", bytes(data[8:12]))[0]
        for r in range(20):
            at = 0xc + 0x18 * r
            if r < count:
                data[at + 0xb] = 0
            else:
                data[at:at + 0x18] = bytes(0x18)
    elif chunk_type == 0x1a:
        data[0x22:0x24] = b"\0\0"
    return (chunk_type, sub, bytes(data))


SAVE_SETUP_CALLEES = {
    0x0049edf0: (1, "stdcall", "probe", _gs_assign, lambda m, ecx, args: (args[0],)),  # operator=
    0x004a6540: (3, "stdcall", "probe", None, _gs_record_chunk),                    # CMapFile::WriteChunk
}


def save_setup_settings(rng, pool, args):
    count = rng.choice([0, 1, 2, 5, 8, 20, rng.randrange(21)])
    return [(GS_VECTOR, FAKE_VTABLE, 4), (GS_VECTOR + 8, WORLD_BASE + GS_SETUPS, 4),
            (GS_VECTOR + 0xc, count + rng.randrange(3), 4), (GS_VECTOR + 0x10, count, 4),
            (GS_VECTOR + 0x14, count, 4)]


# SaveSetup's inlined vector access runs only for indexes below the length
# (unsigned), so the growth of an empty vector (0x433520..0x43363b) is dead;
# the clamping of an out-of-range index (0x433518, 0x433642..0x43364c) needs
# a length of 0x80000001 or more (the signed range check), and such a loop
# would run 2^31 times. The same inlined code is covered by the
# GameSettings_PlayerSetup spec (our two functions share it).
SAVE_SETUP_DEAD_BLOCKS = (
    0x433518, 0x433520, 0x433539, 0x433545, 0x43354d, 0x433568, 0x433576, 0x433584, 0x4335a4, 0x4335b3,
    0x4335c8, 0x4335d1, 0x4335d3, 0x4335ed, 0x4335f1, 0x43361a, 0x43362d, 0x433632, 0x433642, 0x433646,
    0x43364a, 0x43364c)

SPECS += [
    # Dead block: 0x435315, the length clamp after the growth (the length is
    # 0 on that path).
    Spec("GameSettings_PlayerSetup", 0x004351e0, "thiscall", [setup_index], presets=player_setup_vector,
         callees={**GS_LOCK_CALLEES, **GS_ELEMENT_CALLEES}, virtuals={4: 1}, new_fails=0.1,
         new_fails_size=GS_SETUP_BYTES, exempt_blocks=(0x435315,)),
    Spec("GameSettings_SaveSetup", 0x00433490, "thiscall", [lambda rng, pool: WORLD_BASE + SCRATCH, anything,
                                                             anything],
         eax_mask=0, presets=save_setup_settings, callees=SAVE_SETUP_CALLEES, virtuals={4: 1},
         exempt_blocks=SAVE_SETUP_DEAD_BLOCKS),
]


# -- GameSettings::RandomMapSize / RandomMapSymmetry, GamePlayerSetup's
# deleting destructor (game_settings.cpp, session 55) -------------------------
# The settings object is the world; its map file (+0x84) a scratch pointer.
# CMapFile::GetChunk (verified) is probed: it returns chunk 5 (a lazily
# random scratch buffer) or 0; ReleaseChunk is probed. The destructor
# 0x49ec80 (verified) is probed in the deleting destructor.
GS_MAP_FILE = SCRATCH + 0x3000
GS_CHUNK5 = SCRATCH + 0x4000
GS_CHUNK_CALLEES = {
    0x004a60f0: (2, "thiscall", lambda h: 0 if h % 4 == 0 else WORLD_BASE + GS_CHUNK5),  # GetChunk
    0x004a6310: (2, "thiscall", "probe"),                                                # ReleaseChunk
}


def random_map_settings(rng, pool, args):
    return [(0x84, WORLD_BASE + GS_MAP_FILE, 4)]


SPECS += [
    Spec("GameSettings_RandomMapSize", 0x00436ff0, "thiscall", [], presets=random_map_settings,
         callees=GS_CHUNK_CALLEES),
    Spec("GameSettings_RandomMapSymmetry", 0x00437020, "thiscall", [], presets=random_map_settings,
         callees=GS_CHUNK_CALLEES),
    Spec("GamePlayerSetup_ScalarDeletingDestructor", 0x00437fc0, "thiscall", [below(4)],
         callees={0x0049ec80: (0, "thiscall", "probe")}),
]


# -- GamePlayerSetup::GamePlayerSetup (game_settings_players.cpp, session 55) --
# The record is the world; the base vector constructor 0x462df0 (ours,
# verified) runs. ignore_bytes: the three allocator bytes are one
# uninitialized local byte of the original's frame (ours writes 0); the
# self-test pairs WrongGamePlayerSetupConstruct, which also writes the padding
# byte +0x19 next to the first.
SETUP_ALLOCATOR_BYTES = (0x18, 0x28, 0xbc)

SPECS += [
    Spec("GamePlayerSetup_Construct", 0x0049e8f0, "thiscall", [], ignore_bytes=SETUP_ALLOCATOR_BYTES,
         new_fails=0.1),
]
SELF_TEST += [
    Spec("WrongGamePlayerSetupConstruct", 0x0049e8f0, "thiscall", [], ignore_bytes=SETUP_ALLOCATOR_BYTES,
         max_cases=50),
]

# -- session 56 (wip/ai-agents): the credits' debug reports and timer (credits_debug.cpp) --
#
# Texts in scratch slots 0 / 1 (args point there); OutputDebugStringA is an
# API probe (records the text), sprintf runs. The timer at world +0 (ECX):
# its owner in scratch 2 with its vtable at emu.FAKE_VTABLE (slot 0: one
# stack argument).

CREDITS_WORDS = (b"", b"OutOfMemory", b"file klemmt(fehlt?)", b"E:/Siedler3/Credits/cmy_x.cpp", b"a%sb")


def credits_texts(rng, pool, args):
    fields = []
    for slot in (0, 1):
        text = rng.choice(CREDITS_WORDS) + b"\0"
        fields += [(SCRATCH + slot * SCRATCH_SLOT_BYTES + k, byte, 1) for k, byte in enumerate(text)]
    return fields


def credits_line(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.3 else rng.randrange(3000)


def credits_timer(rng, pool, args):
    interval = rng.randint(-3, 0) if rng.random() < 0.1 else rng.randint(1, 40)
    catch_up = 0 if rng.random() < 0.5 else rng.randint(1, 255)
    if catch_up and interval <= 0:
        interval = 1  # (the original would loop forever)
    now = args[0] if args else 0
    signed_now = now - (1 << 32) if now & 0x80000000 else now
    nxt = (signed_now + rng.randint(-200, 50)) & 0xFFFFFFFF
    return [(0, WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES, 4), (4, interval & 0xFFFFFFFF, 4), (8, nxt, 4),
            (0xC, catch_up, 1), (0xD, 0 if rng.random() < 0.5 else rng.randint(1, 255), 1),
            (SCRATCH + 2 * SCRATCH_SLOT_BYTES, FAKE_VTABLE, 4)]


def credits_now(rng, pool):
    return rng.randint(-1000, 1000) & 0xFFFFFFFF


SPECS += [
    Spec("Credits_DebugPrint", 0x0059e6f0, "fastcall", [scratch(0)], eax_mask=0, presets=credits_texts),
    Spec("Credits_DebugPrintLine", 0x0059e700, "fastcall", [scratch(0)], eax_mask=0, presets=credits_texts),
    Spec("Credits_DebugPrint2", 0x0059e720, "fastcall", [scratch(0), scratch(1)], eax_mask=0,
         presets=credits_texts),
    Spec("Credits_DebugPrintFileLine", 0x0059e730, "fastcall", [scratch(0), credits_line], eax_mask=0,
         presets=credits_texts),
    Spec("Credits_ReportCaughtUnknown", 0x0059e760, "fastcall", [scratch(0), scratch(1), credits_line],
         eax_mask=0, presets=credits_texts),
    Spec("Credits_ReportCaughtException", 0x0059e790, "fastcall", [anything, scratch(0), scratch(1), credits_line],
         eax_mask=0, presets=credits_texts),
    Spec("Credits_ReportCreatingException", 0x0059e7d0, "fastcall", [scratch(0), scratch(1), credits_line],
         eax_mask=0, presets=credits_texts),
    Spec("cmy_impuls_Timer_Construct", 0x0059f210, "thiscall", [anything]),
    Spec("cmy_impuls_Timer_Fire", 0x0059f230, "thiscall", [credits_now], presets=credits_timer, virtuals={0: 1}),
    Spec("cmy_impuls_Timer_Update", 0x0059f250, "thiscall", [credits_now], eax_mask=0, presets=credits_timer,
         virtuals={0: 1}),
    Spec("cmy_impuls_Timer_Restart", 0x0059f2a0, "thiscall", [credits_now], presets=credits_timer),
]


# -- Dirk::CTeam and the team/player comparisons (game_teams.cpp, session 56) --
# The team at world offset 0: its vectors' blocks in scratch slots 0 and 1
# (or none), filled -2..6; ~GamePlayerSetup probed.


def team_to_destroy(rng, pool, args):
    return [(0x8 + 8, rng.choice([0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH]), 4),
            (0x8 + 0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4),
            (0x24 + 8, rng.choice([0, WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES] * 2 + [0]), 4),
            (0x24 + 0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4)]


def equal_or_close_keys(offset):
    def presets(rng, pool, args):
        x = rng.getrandbits(32)
        y = rng.choice([x, x, (x + 1) & 0xFFFFFFFF, (x - 1) & 0xFFFFFFFF, rng.getrandbits(32), 0x80000000])
        return [(offset, x, 4), (SCRATCH + offset, y, 4)]
    return presets


SPECS += [
    Spec("CTeam_Construct", 0x004a0200, "thiscall", [], new_fails=0.2),
    Spec("CTeam_ConstructId", 0x004a02d0, "thiscall", [anything], new_fails=0.2),
    Spec("CTeam_Destroy", 0x004a04d0, "thiscall", [], presets=team_to_destroy,
         callees={0x0049ec80: (0, "thiscall", "probe")}),
    Spec("CTeam_Less", 0x004a05a0, "thiscall", [scratch(0)], presets=equal_or_close_keys(0)),
    Spec("GamePlayerSetup_Equal", 0x0049f100, "thiscall", [scratch(0)], presets=equal_or_close_keys(0x10)),
]


# -- small control members (ui_control_members.cpp, session 56) ------------
#
# `this` at MEMBER_THIS (world offset; the controls' own fields before it are
# lazily random), vbptrs at this + delta pointing at a vbtable in scratch
# slot 6 whose entry [1] keeps the virtual base near `this`. Every callee is
# probed (SetRect's rect argument recorded by its contents: it's a local of
# the caller).

MEMBER_THIS = SCRATCH + 4 * SCRATCH_SLOT_BYTES + 0x800
MEMBER_VBTABLE = SCRATCH + 6 * SCRATCH_SLOT_BYTES
MEMBER_CALLEES = {
    0x415af0: (3, "thiscall", "probe"), 0x4168c0: (0, "thiscall", "probe"), 0x5faaa0: (2, "thiscall", "probe"),
    0x413d70: (1, "thiscall", "probe"), 0x415c80: (5, "thiscall", "probe"), 0x418380: (5, "thiscall", "probe"),
    0x417ab0: (2, "thiscall", "probe"), 0x417be0: (1, "thiscall", "probe"),
    0x4156e0: (1, "thiscall", "probe", None, lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)),)),
    0x41c3c0: (1, "thiscall", "probe"), 0x4180a0: (0, "thiscall", "probe"),
}


def member_state(delta=None, fields=()):
    def presets(rng, pool, args):
        out = [(offset, value(rng) if callable(value) else value, size) for offset, value, size in fields]
        if delta is not None:
            out += [(MEMBER_THIS + delta, WORLD_BASE + MEMBER_VBTABLE, 4),
                    (MEMBER_VBTABLE + 4, (-delta + rng.randint(-0x40, 0x40)) & 0xFFFFFFFF, 4)]
        return out
    return presets


def member_vbptrs(*deltas):
    """Presets: vbptrs at this + delta, all pointing at the member vbtable
    (one displacement, so their bases sit at distinct places), plus the
    rect at MEMBER_A sometimes small (the up-down's halves round both
    ways)."""
    def presets(rng, pool, args):
        out = [(MEMBER_THIS + delta, WORLD_BASE + MEMBER_VBTABLE, 4) for delta in deltas]
        out.append((MEMBER_VBTABLE + 4, rng.randint(0x600, 0x700), 4))
        if rng.random() < 0.5:
            out += [(MEMBER_A + 4 * k, rng.randint(-9, 9) & 0xFFFFFFFF, 4) for k in range(4)]
        return out
    return presets


def member_spec(name, address, args, this=MEMBER_THIS, **kwargs):
    kwargs.setdefault("callees", MEMBER_CALLEES)
    return Spec(name, address, "fastcall", [one_of(WORLD_BASE + this), anything, *args], **kwargs)


MEMBER_A = SCRATCH + 4 * SCRATCH_SLOT_BYTES + 0x100   # a second object / a rect
MEMBER_B = SCRATCH + 4 * SCRATCH_SLOT_BYTES + 0x200
maybe_zero = lambda rng: rng.choice([0, rng.getrandbits(32)])  # noqa: E731


def words_move(rng, pool):
    return WORLD_BASE + MEMBER_A + 2 * rng.randrange(64)


SPECS += [
    member_spec("CControl_Draw", 0x004187d0, [anything, anything, anything], presets=member_state(-0x38)),
    # Only AL is defined in the original (the rest is the caller's EAX; both callers overwrite it).
    member_spec("CControl_SetEnabled", 0x00418810, [lambda rng, pool: rng.choice([0, 1, 0x100, rng.getrandbits(32)])],
                eax_mask=0xFF),
    member_spec("CControl_SetTooltipTextId", 0x00418330, [lambda rng, pool: maybe_zero(rng)],
                presets=member_state(4)),
    member_spec("CScreen_HasQueuedMessages", 0x00417f50, [],
                presets=member_state(fields=((MEMBER_THIS + 0x84, maybe_zero, 4),))),
    # 0x419b5f (the enabled branch) is listed as a function start, but is this function's code.
    member_spec("CEditControl_OnLButtonDown", 0x00419b30, [anything] * 5, cover_until=0x00419b7c,
                presets=member_state(-0x6c, ((MEMBER_THIS - 0x64, lambda rng: rng.choice([0, 1, 0x80]), 1),))),
    member_spec("CVScrollBar_OnMoved", 0x0041e9e0, [one_of(WORLD_BASE + MEMBER_A), anything]),
    member_spec("CDropdown_SetFlag1F", 0x00422490, [anything], presets=member_state(-0x668)),
    member_spec("CListView_GetHeaderHeight", 0x00424590, [], this=MEMBER_A,
                presets=member_state(fields=((MEMBER_A + 0x4f2, lambda rng: rng.choice([0, 1, 0xff]), 1),))),
    member_spec("CColumn_CopyConstruct", 0x004275a0, [one_of(WORLD_BASE + MEMBER_A)], this=MEMBER_B),
    # The assignments sometimes onto the source itself (which does nothing).
    Spec("CColumn_Assign", 0x00427600, "fastcall",
         [one_of(WORLD_BASE + MEMBER_A, WORLD_BASE + MEMBER_B), anything, one_of(WORLD_BASE + MEMBER_A)]),
    member_spec("CLine_CopyConstruct", 0x004277a0, [one_of(WORLD_BASE + MEMBER_A)], this=MEMBER_B),
    Spec("CLine_Assign", 0x00427800, "fastcall",
         [one_of(WORLD_BASE + MEMBER_A, WORLD_BASE + MEMBER_B), anything, one_of(WORLD_BASE + MEMBER_A)]),
    member_spec("CColumn_Equal", 0x00427830, [one_of(WORLD_BASE + MEMBER_B)], this=MEMBER_A,
                presets=lambda rng, pool, args: [(MEMBER_A, (x := rng.getrandbits(32)), 4),
                                                 (MEMBER_B, x if rng.random() < 0.5 else rng.getrandbits(32), 4)]),
    member_spec("CColumn_SetPosition", 0x00427650, [anything, anything, anything], this=MEMBER_A,
                presets=member_state(fields=((MEMBER_A + 0x1c, maybe_zero, 4),))),
    member_spec("CStaticImage_Construct", 0x0041d7a0, [one_of(WORLD_BASE + MEMBER_A), anything]),
    member_spec("CStaticFrame_Construct", 0x0041d960, [one_of(WORLD_BASE + MEMBER_A), anything, anything]),
    member_spec("CStaticColorIndexedFrame_Construct", 0x0041db90,
                [one_of(WORLD_BASE + MEMBER_A), anything, anything, anything]),
    member_spec("CVScrollBar_OnResized", 0x0041e940, [one_of(WORLD_BASE + MEMBER_A), anything],
                presets=member_vbptrs(-0x238, -0x1b8)),
    member_spec("CDropdown_OnResized", 0x004224c0, [one_of(WORLD_BASE + MEMBER_A), anything],
                presets=member_vbptrs(-0x5b0, -0x520, -0x4a0)),
    member_spec("CUpDown_OnResized", 0x00422d90, [one_of(WORLD_BASE + MEMBER_A), anything],
                presets=member_vbptrs(-0x124, -0xa4)),
    # The text's buffer in MEMBER_B (count byte 0, 0xff or shared) or none.
    member_spec("CEditControl_Destroy", 0x00421f50, [],
                presets=member_state(fields=((MEMBER_THIS - 0x20, lambda rng: rng.choice([0, WORLD_BASE + MEMBER_B + 2]), 4),
                                             (MEMBER_B + 1, lambda rng: rng.choice([0, 0xff, 1, 2, 0xfe]), 1)))),
    Spec("UI_MoveWords", 0x00418cf0, "fastcall",
         [lambda rng, pool: WORLD_BASE + MEMBER_A + 2 * rng.randrange(-20, 84), words_move,
          lambda rng, pool: rng.randrange(40)]),
]


# GamePlayerSetup(id) (session 56): the allocator bytes are the id's low byte.
SPECS += [
    Spec("GamePlayerSetup_ConstructId", 0x0049e9f0, "thiscall", [anything], new_fails=0.1),
]


# Dirk::CTeam::PlayerIds (session 56): the team at world offset 0, its int
# vector's block in scratch slot 0 (capacity 0..8 or not positive, filled
# -1..capacity), the result in scratch slot 5.
def team_ids_state(rng, pool, args):
    capacity = rng.choice([0x80000000, 0xFFFFFFFF] + list(range(9)))
    filled = rng.randint(-1, capacity if capacity <= 8 else 8)
    return [(8 + 8, WORLD_BASE + SCRATCH, 4), (8 + 0xc, capacity, 4), (8 + 0x10, filled & 0xFFFFFFFF, 4)]


SPECS += [Spec("CTeam_PlayerIds", 0x004a07b0, "thiscall", [scratch(5)], presets=team_ids_state, new_fails=0.1)]


# ~CStaticText (session 56): the label at world offset 0, its text at +0x28
# any kind of string (common.string_fields, buffer in scratch slot 0);
# ~CScreenElement probed.
def static_text_to_destroy(rng, pool, args):
    fields, _ = string_fields(rng, 0x28, SCRATCH)
    return fields


# Dead (DESTROY, see msvc6.py): the inlined ~string's _Eos after erase(0,
# npos) (_Grow(0) is false) and its release's decrement (_Split left the
# buffer unshared or frozen).
SPECS += [Spec("CStaticText_Destroy", 0x0041c580, "thiscall", [], presets=static_text_to_destroy,
               callees={0x004156b0: (0, "thiscall", "probe")}, exempt_blocks=(0x0041c61d, 0x0041c63f))]


# -- GamePlayerSetup::Set, copy constructor, operator= (session 57) ----------
# Set: the record at world offset 0, its "initial" party and race (+0x70,
# +0x6c) often -1.
def setup_set_state(rng, pool, args):
    return [(at, rng.choice([0xFFFFFFFF, rng.getrandbits(32)]), 4) for at in (0x6c, 0x70)]


# Copy: `this` at world offset 0 (its strings' buffers in scratch slots
# 14..16), the source at scratch slot 10 (buffers in slots 11..13, the
# strings sometimes sharing `this`'s); std::string at +0x18 and +0xbc, the
# 16-bit string at +0x28. Both string assigns and the vector constructor
# 0x462df0 run (ours against the originals).
SETUP_COPY_FROM = SCRATCH + 10 * SCRATCH_SLOT_BYTES


def setup_copy_strings(rng, pool, args):
    fields, ptrs = [], {}
    for k, offset in enumerate((0x18, 0x28, 0xbc)):
        make = wstring_fields if offset == 0x28 else string_fields
        f, ptrs[offset] = make(rng, offset, SCRATCH + (14 + k) * SCRATCH_SLOT_BYTES)
        fields += f
    for k, offset in enumerate((0x18, 0x28, 0xbc)):
        make = wstring_fields if offset == 0x28 else string_fields
        f, _ = make(rng, SETUP_COPY_FROM + offset, SCRATCH + (11 + k) * SCRATCH_SLOT_BYTES, shared_ptr=ptrs[offset])
        fields += f
    return fields


def setup_copy_source(self_chance: float):
    def generate(rng, pool):
        return WORLD_BASE if rng.random() < self_chance else WORLD_BASE + SETUP_COPY_FROM
    return generate


SPECS += [
    Spec("GamePlayerSetup_Set", 0x0049f120, "thiscall", [anything] * 9, presets=setup_set_state),
    Spec("GamePlayerSetup_CopyConstruct", 0x0049eaf0, "thiscall", [setup_copy_source(0.0)],
         presets=setup_copy_strings, globals=null_string_count, new_fails=0.05),
    # Dead in the three inlined assigns: 0x49ee32, 0x49ef20, 0x49f038 (a
    # length above npos 0xffffffff) and 0x49ee3c, 0x49ef2a, 0x49f042 (the
    # member is its own source: `this` != from there).
    Spec("GamePlayerSetup_Assign", 0x0049edf0, "thiscall", [setup_copy_source(0.1)],
         presets=setup_copy_strings, globals=null_string_count,
         exempt_blocks=(0x0049ee32, 0x0049ee3c, 0x0049ef20, 0x0049ef2a, 0x0049f038, 0x0049f042)),
]


# -- The vectors of player setups and teams (game_settings_vectors.cpp, session 57) --
# The vector at world offset 0, its block at GV_BLOCK (lazily random
# elements); the element members are probed (verified on their own):
# copy constructors record destination and source.
GV_BLOCK = 0x10000
GV_VALUE = SCRATCH
GV_PLAYER_CALLEES = {
    0x0049eaf0: (1, "thiscall", "this"),   # GamePlayerSetup copy constructor
    0x0049edf0: (1, "thiscall", "this"),   # GamePlayerSetup::operator=
    0x0049ec80: (0, "thiscall", "probe"),  # ~GamePlayerSetup
    0x00437fc0: (1, "thiscall", "probe"),  # GamePlayerSetup scalar deleting destructor
}
GV_TEAM_CALLEES = {
    0x004a03a0: (1, "thiscall", "this"),   # CTeam copy constructor
    0x004a05c0: (1, "thiscall", "this"),   # CTeam::operator=
    0x004a04d0: (0, "thiscall", "probe"),  # ~CTeam
    0x00414b90: (1, "thiscall", "probe"),  # CTeam scalar deleting destructor
}


def gv_vector_fields(at: int, block: int):
    def preset(rng, pool, args):
        capacity = rng.choice([0, 1, 2, 3, 5, rng.randrange(8)])
        filled = rng.choice([0, 1, 2, -1, rng.randrange(capacity + 1), capacity])
        length = rng.choice([capacity, capacity, rng.randrange(capacity + 1), rng.randrange(8)])
        data = rng.choice([0, WORLD_BASE + block, WORLD_BASE + block, WORLD_BASE + block])
        # Also steps that wrap the capacity around (to 0 or below the length).
        grow = rng.choice([0, 1, 2, 5, rng.getrandbits(32), 0xFFFFFFFF, (-capacity) & 0xFFFFFFFF])
        return [(at + 4, rng.getrandbits(32), 4), (at + 8, data, 4), (at + 0xc, capacity, 4),
                (at + 0x10, filled & 0xFFFFFFFF, 4), (at + 0x14, length, 4), (at + 0x18, grow, 4)]
    return preset


def gv_count(rng, pool):
    return rng.choice([0, 1, 2, 3, 4, 6, rng.randrange(10), 0x10000000 + rng.getrandbits(20)])


def gv_index(rng, pool):
    return rng.choice([0, 1, 2, 3, 5, -1, -2, rng.randrange(8), rng.getrandbits(32)]) & 0xFFFFFFFF


# CTeam at `at`: id, unk04, the id vector (ints at block) and the player
# vector (elements at block + 0x8000).
def gv_team(at: int, block: int, fake_ids_vtable: bool = False):
    ids = gv_vector_fields(at + 8, block)
    players = gv_vector_fields(at + 0x24, block + 0x8000)

    def preset(rng, pool, args):
        fields = [(at, rng.getrandbits(32), 4), (at + 4, rng.getrandbits(32), 4)]
        fields += ids(rng, pool, args) + players(rng, pool, args)
        if fake_ids_vtable:
            fields.append((at + 8, FAKE_VTABLE, 4))
        return fields
    return preset


def gv_both_teams(rng, pool, args):
    return gv_team(0, 0x10000)(rng, pool, args) + gv_team(SCRATCH, 0x30000)(rng, pool, args)


def gv_copy_source(rng, pool, args):
    return gv_vector_fields(SCRATCH, 0x30000)(rng, pool, args)


def gv_find_effect(machine, ecx, args, h):
    """The sorted vector's find (slot 3): writes an insert position (also
    out of range) to its third argument."""
    machine.guest_write(args[2], struct.pack("<i", (h % 10) - 2))


SPECS += [
    Spec("PlayerVector_Resize", 0x00437820, "thiscall", [gv_count], presets=gv_vector_fields(0, GV_BLOCK),
         callees=GV_PLAYER_CALLEES, new_fails=0.1),
    Spec("TeamVector_Resize", 0x00437d00, "thiscall", [gv_count], presets=gv_vector_fields(0, GV_BLOCK),
         callees=GV_TEAM_CALLEES, new_fails=0.1),
    Spec("PlayerVector_InsertAt", 0x004379e0, "thiscall", [gv_index, one_of(WORLD_BASE + GV_VALUE)],
         presets=gv_vector_fields(0, GV_BLOCK), callees=GV_PLAYER_CALLEES, new_fails=0.1),
    Spec("TeamVector_InsertAt", 0x00438090, "thiscall", [gv_index, one_of(WORLD_BASE + GV_VALUE)],
         presets=gv_vector_fields(0, GV_BLOCK), callees=GV_TEAM_CALLEES, new_fails=0.1),
    Spec("PlayerBareVector_CopyContents", 0x004a13d0, "thiscall", [one_of(WORLD_BASE + SCRATCH)],
         presets=gv_copy_source, callees=GV_PLAYER_CALLEES, new_fails=0.1, eax_mask=0, cover_until=0x004a14a0),
    Spec("CTeam_CopyConstruct", 0x004a03a0, "thiscall", [one_of(WORLD_BASE + SCRATCH)], presets=gv_both_teams,
         callees=GV_PLAYER_CALLEES, new_fails=0.1),
    Spec("CTeam_Assign", 0x004a05c0, "thiscall", [one_of(WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH, WORLD_BASE)],
         presets=gv_both_teams, callees=GV_PLAYER_CALLEES, new_fails=0.1),
    Spec("CTeam_AddPlayerId", 0x004a06d0, "thiscall", [anything], presets=gv_team(0, 0x10000, True),
         virtuals={0x0c: (3, {0: 4, 2: 4})}, virtual_effects={0x0c: gv_find_effect}, new_fails=0.1),
    Spec("GamePlayerSetup_LogSlot", 0x004a01c0, "thiscall", [],
         presets=lambda rng, pool, args: string_fields(rng, 0xbc, SCRATCH)[0]),
]


# -- CControl, CButton, CImageButton, the screen's message queue (controls.cpp, session 57) --
#
# The object starts at world offset UICTL_OBJECT; its vbptr (+4) points at a
# vbtable in scratch slot 6 whose displacement puts the CScreenElement
# virtual base at the class's size or a little further (never inside the
# class's own fields: the vtable stores would overwrite them). Functors and
# the tooltip are fake objects (vtable emu.FAKE_VTABLE, slot 0 with one
# argument) in scratch slot 7. The base constructors, SetRect (rects
# recorded by contents), IsOffScreen, ShowTooltip (GDI) and the class
# constructors/destructors called by the derived ones are probed.

UICTL_OBJECT = 0x400
UICTL_VBTABLE = SCRATCH + 6 * SCRATCH_SLOT_BYTES
UICTL_FAKES = SCRATCH + 7 * SCRATCH_SLOT_BYTES
UICTL_RECT_PROBE = (1, "thiscall", "probe", None, lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)),))
UICTL_CALLEES = {0x415610: (0, "thiscall", "probe"), 0x415660: UICTL_RECT_PROBE, 0x4156e0: UICTL_RECT_PROBE,
                 0x415840: (0, "thiscall", "probe"), 0x418550: (0, "thiscall", "probe"),
                 0x417fe0: (5, "thiscall", "probe"), 0x4180a0: (0, "thiscall", "probe"),
                 0x4183c0: (0, "thiscall", "probe"), 0x41a830: (6, "thiscall", "probe"),
                 0x4168c0: (0, "thiscall", "probe"), 0x5faaa0: (2, "thiscall", lambda h: uictl_text(h)),
                 0x5fad20: (1, "thiscall", "probe"), 0x41c3c0: (1, "thiscall", "probe"),
                 0x41abe0: (7, "thiscall", "probe"), 0x41a900: (0, "thiscall", "probe"),
                 0x41b080: (0, "thiscall", "probe"), 0x428bd0: (0, "thiscall", "probe"),
                 0x428bf0: (0, "thiscall", "probe"), 0x428e70: (1, "thiscall", "probe")}
UICTL_TEXT = SCRATCH + 0x300  # TextAddress's text (random letters)


def uictl_text(h):
    return 0 if h % 4 == 0 else WORLD_BASE + UICTL_TEXT


def uictl_callees(own):
    """The probed callees, without the function itself."""
    return {address: entry for address, entry in UICTL_CALLEES.items() if address != own}


def uictl_fake(k):
    return WORLD_BASE + UICTL_FAKES + 0x10 * k


def uictl_objects(size, fields=(), vbptr=UICTL_OBJECT + 4):
    """Presets: the vbtable (base at `size` from the object, +0..16) and the
    fake objects; `fields` (object offset, generator(rng, element), size),
    where element is the virtual base's world offset."""
    def presets(rng, pool, args):
        displacement = (UICTL_OBJECT + size) - vbptr + 4 * rng.randrange(5)
        element = vbptr + displacement
        out = [(vbptr, WORLD_BASE + UICTL_VBTABLE, 4), (UICTL_VBTABLE + 4, displacement & 0xFFFFFFFF, 4)]
        out += [(UICTL_FAKES + 0x10 * k, FAKE_VTABLE, 4) for k in range(3)]
        for offset, value, width in fields:
            out.append((UICTL_OBJECT + offset if offset is not None else element + 0x1d,
                        value(rng) & ((1 << (8 * width)) - 1), width))
        return out
    return presets


def uictl_rect(rng, pool):
    return rng.choice([0, 5, 0x7fffffff, 0x80000000, rng.randint(-50, 700) & 0xFFFFFFFF, rng.getrandbits(32)])


def uictl_most_derived(rng, pool):
    return rng.choice([0, 0, 1, 0x100, rng.getrandbits(32)])


def uictl_control_to_destroy(size):
    base = uictl_objects(size, ((0x20, lambda rng: rng.choice([0, uictl_fake(0)]), 4),))

    def presets(rng, pool, args):
        fields, _ = string_fields(rng, UICTL_OBJECT + 0x10, SCRATCH)
        return base(rng, pool, args) + fields
    return presets


def uictl_link(h):
    return 0 if h % 4 == 0 else WORLD_BASE + SCRATCH + 0x800


UICTL_OBJ = WORLD_BASE + UICTL_OBJECT
UICTL_ELEMENT = lambda size: one_of(UICTL_OBJ + size)  # noqa: E731  (this = the element part, no jitter: probes only)
UICTL_TIMER_FIELDS = ((None, lambda rng: rng.choice([0, 0, 1, 0x80]), 1),  # the element's mouse_inside
                      (0x20, lambda rng: rng.choice([0, 0, uictl_fake(0)]), 4),
                      (0x08, lambda rng: rng.choice([0, uictl_fake(1)]), 4))

SPECS += [
    Spec("CControl_Construct", 0x00417fe0, "fastcall",
         [one_of(UICTL_OBJ), anything] + [uictl_rect] * 4 + [uictl_most_derived],
         presets=uictl_objects(0x3c), callees=uictl_callees(0x00417fe0)),
    # Dead (as in CStaticText_Destroy): the inlined ~string's _Eos after
    # erase(0, npos) and its release's decrement.
    Spec("CControl_Destroy", 0x004180a0, "fastcall", [one_of(UICTL_OBJ + 0x3c), anything], eax_mask=0,
         presets=uictl_control_to_destroy(0x3c), virtuals={0: 1}, callees=uictl_callees(0x004180a0),
         exempt_blocks=(0x00418198, 0x004181b7)),
    # Exempt 0x41840d: the functor call for a null control (this == 0x3c),
    # whose fields would sit at address 0 (not a real control; ours keeps
    # the check).
    Spec("CControl_OnTimer", 0x004183c0, "fastcall", [one_of(UICTL_OBJ + 0x3c), anything], eax_mask=0xFF,
         presets=uictl_objects(0x3c, UICTL_TIMER_FIELDS), virtuals={0: 1}, callees=uictl_callees(0x004183c0),
         exempt_blocks=(0x0041840d,)),
    # The group part's element at object + 0x3c + 0x7c, its vbptr 0xb4 before.
    Spec("CGroupControl_Tick", 0x00429f10, "fastcall", [one_of(UICTL_OBJ + 0xb8), anything],
         presets=uictl_objects(0x100, vbptr=UICTL_OBJECT + 0xb8 - 0xb4), callees=uictl_callees(0x00429f10)),
    Spec("CButton_Construct", 0x0041a830, "fastcall",
         [one_of(UICTL_OBJ), anything] + [uictl_rect] * 4 + [anything, uictl_most_derived],
         presets=uictl_objects(0x50), callees=uictl_callees(0x0041a830)),
    Spec("CButton_Destroy", 0x0041a900, "fastcall", [one_of(UICTL_OBJ + 0x50), anything],
         presets=uictl_objects(0x50, ((0x49, lambda rng: rng.choice([0, 1, 0x80]), 1),
                                      (0x40, lambda rng: rng.choice([0, uictl_fake(2)]), 4))),
         virtuals={0: 1}, callees=uictl_callees(0x0041a900)),
    Spec("CImageButton_Construct", 0x0041abe0, "fastcall",
         [one_of(UICTL_OBJ), anything, one_of(WORLD_BASE + SCRATCH + 0x100)] + [anything] * 5 + [uictl_most_derived],
         presets=uictl_objects(0x60), callees=uictl_callees(0x0041abe0)),
    # The list's allocator (+0x10): 0 (operator new, sometimes failing) or a
    # stand-in at 0x59f200 (an empty function; probed here) that hands out a
    # link in scratch or none.
    Spec("MsgConsoleList_NewLink", 0x00429900, "fastcall",
         [one_of(UICTL_OBJ), anything, one_of(WORLD_BASE + SCRATCH + 0x100)], new_fails=0.2,
         presets=lambda rng, pool, args: [(UICTL_OBJECT + 0x10, rng.choice([0, 0x59f200]), 4)],
         callees={0x59f200: (0, "thiscall", uictl_link)}),
    # NewLink gets the address of the by-value message (a stack slot):
    # recorded by its contents.
    Spec("CScreen_PostMessage", 0x00417fa0, "fastcall", [one_of(UICTL_OBJ + 0x1c), anything] + [anything] * 5,
         callees={0x429900: (1, "thiscall", uictl_link, None, lambda m, ecx, args: (bytes(m.guest_read(args[0], 20)),)),
                  0x60b430: (1, "thiscall", "probe")}),
]


# Session 57, second part: CScreenGroup, CTextButton, CStaticText (text id),
# CAnimatedButton (controls.cpp). TextAddress returns the text at
# UICTL_TEXT or null; CTextButton's string argument is a std::string at
# scratch + 0x400 (common.string_fields, buffer at scratch + 0x500).


def uictl_texts(size, string_at=None):
    base = uictl_objects(size)

    def presets(rng, pool, args):
        out = base(rng, pool, args)
        length = rng.choice([0, 1, rng.randrange(40)])
        out += [(UICTL_TEXT + i, rng.randint(1, 255), 1) for i in range(length)] + [(UICTL_TEXT + length, 0, 1)]
        if string_at is not None:
            fields, _ = string_fields(rng, string_at, SCRATCH + 0x500)
            out += fields
        return out
    return presets


def uictl_rect_ptr(rng, pool):
    return WORLD_BASE + SCRATCH + 0x100 + 4 * rng.randrange(4)


def uictl_text_button_to_destroy(rng, pool, args):
    fields, _ = string_fields(rng, UICTL_OBJECT + 0x5c, SCRATCH)
    return fields


def uictl_sequence(rng, pool):
    return rng.choice([0xFFFFFFFF, 0, rng.randrange(300), rng.getrandbits(32)])


SPECS += [
    Spec("CScreenGroup_Construct", 0x00416c70, "fastcall", [one_of(UICTL_OBJ), anything, uictl_rect_ptr, uictl_most_derived],
         presets=uictl_objects(0x80), callees=uictl_callees(0x00416c70)),
    # Exempt: the inlined assign's "argument is this string" branch
    # (0x41b507..0x41b563; the argument would have to be the button's own
    # text member, which the constructor has only just cleared) and its
    # clamp of n to npos (0x41b500, n = the length can't exceed it). The
    # static empty string's count is varied (a null buffer then copies).
    Spec("CTextButton_Construct", 0x0041b470, "fastcall",
         [one_of(UICTL_OBJ), anything, uictl_rect_ptr, one_of(WORLD_BASE + SCRATCH + 0x400), anything, anything,
          uictl_most_derived],
         presets=uictl_texts(0x74, SCRATCH + 0x400), callees=uictl_callees(0x0041b470), globals=null_string_count,
         exempt_blocks=(0x0041b500, 0x0041b507, 0x0041b511, 0x0041b516, 0x0041b526, 0x0041b528, 0x0041b52c, 0x0041b554,
                        0x0041b55c)),
    # The text-id variants' dead blocks: the temporary's release by operator
    # delete (the member always shares the temporary's unshared buffer, so
    # the release only decrements) and the copy of "" (0x7a7868 is empty:
    # _Grow(0) is false).
    Spec("CTextButton_ConstructTextId", 0x0041b640, "fastcall",
         [one_of(UICTL_OBJ), anything, uictl_rect_ptr, anything, anything, anything, uictl_most_derived],
         presets=uictl_texts(0x74), callees=uictl_callees(0x0041b640), exempt_blocks=(0x0041b79f, 0x0041b7cd)),
    # Dead (as in CStaticText_Destroy): the inlined ~string's _Eos after
    # erase(0, npos) and its release's decrement.
    Spec("CTextButton_Destroy", 0x0042ab70, "fastcall", [one_of(UICTL_OBJ + 0x74), anything], eax_mask=0,
         presets=uictl_text_button_to_destroy, callees=uictl_callees(0x0042ab70),
         exempt_blocks=(0x0042ac0d, 0x0042ac2f)),
    Spec("CStaticText_ConstructTextId", 0x0041c680, "fastcall",
         [one_of(UICTL_OBJ), anything, uictl_rect_ptr, anything, lambda rng, pool: rng.choice([0, rng.getrandbits(32)])],
         presets=uictl_texts(0x44), callees=uictl_callees(0x0041c680), exempt_blocks=(0x0041c7a0, 0x0041c7ce)),
    Spec("CAnimatedButton_Construct", 0x0041b0a0, "fastcall",
         [one_of(UICTL_OBJ), anything, uictl_rect_ptr, anything, uictl_sequence, anything, anything, anything,
          uictl_most_derived],
         presets=uictl_objects(0x68), callees=uictl_callees(0x0041b0a0)),
]


def uictl_group_control(rng, pool, args):
    """CGroupControl: the control's vbptr (+4) and the group part's (+0x3c)
    share the vbtable, so the group's base lands 0x38 further."""
    return uictl_objects(0xb8)(rng, pool, args) + [(UICTL_OBJECT + 0x3c, WORLD_BASE + UICTL_VBTABLE, 4)]


# CScreenGroup's constructor, probed with its rect (a local) by contents.
UICTL_CALLEES[0x416c70] = (2, "thiscall", "probe", None,
                           lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)), args[1]))

SPECS += [
    Spec("CGroupControl_Construct", 0x0041a5a0, "fastcall",
         [one_of(UICTL_OBJ), anything] + [uictl_rect] * 4 + [uictl_most_derived],
         presets=uictl_group_control, callees=uictl_callees(0x0041a5a0)),
    Spec("CScreen_Construct", 0x00417d10, "fastcall", [one_of(UICTL_OBJ), anything, uictl_most_derived],
         presets=uictl_objects(0x98), callees=uictl_callees(0x00417d10)),
]


# CListView::Draw / FillBackground, the column vector's base_equivalent and
# WCListDelete (ui_control_members.cpp, session 57). The list view's vbptrs
# at this-0x598, -0xa4, -0x400 share the member vbtable; the rect callees
# are probed with their rects by contents; IntersectRects writes a result
# fixed by its inputs (so the callers' reads of it are compared).


def uictl_intersect(machine, ecx, args, h):
    a = [int.from_bytes(machine.guest_read(args[1] + 4 * k, 4), "little") for k in range(4)]
    b = [int.from_bytes(machine.guest_read(args[2] + 4 * k, 4), "little") for k in range(4)]
    out = [a[0] ^ b[1], (a[1] + b[0]) & 0xFFFFFFFF, (a[2] - b[3]) & 0xFFFFFFFF, a[3] ^ b[2]]
    machine.guest_write(args[0], b"".join(v.to_bytes(4, "little") for v in out))
    return args[0]


def uictl_rect_arg(index):
    return lambda m, ecx, args: tuple(bytes(m.guest_read(a, 16)) if i == index else a for i, a in enumerate(args))


LIST_VIEW_CALLEES = {
    0x415af0: (3, "thiscall", "probe"),
    0x415aa0: (2, "thiscall", "probe", None, uictl_rect_arg(1)),
    0x4156d0: (0, "thiscall", lambda h: WORLD_BASE + MEMBER_A + 0x40),
    0x415b20: (3, "thiscall", "probe", uictl_intersect,
               lambda m, ecx, args: (bytes(m.guest_read(args[1], 16)), bytes(m.guest_read(args[2], 16)))),
    0x417480: (3, "thiscall", "probe", None, uictl_rect_arg(1)),
    0x417460: (2, "thiscall", "probe"),
    0x5bf0c0: (5, "thiscall", "probe", None,
               lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)), args[1], args[2], args[3],
                                     bytes(m.guest_read(args[4], 4)), bytes(m.guest_read(args[4] + 0x50, 4)))),
    0x427830: (1, "thiscall", "probe"),
}


def list_view_state(rng, pool, args):
    displacement = rng.randint(0x600, 0x700)
    out = [(MEMBER_THIS + d, WORLD_BASE + MEMBER_VBTABLE, 4) for d in (-0x598, -0xa4, -0x400)]
    out += [(MEMBER_VBTABLE + 4, displacement, 4),
            (MEMBER_THIS - 0x598 + displacement + 0x1f, rng.choice([0, 0, 1]), 1),
            (MEMBER_THIS - 0xaa, rng.choice([0, 1]), 1), (MEMBER_THIS - 0xac, rng.choice([0, 0x80]), 1)]
    return out


SPECS += [
    Spec("CListView_Draw", 0x004267c0, "fastcall",
         [one_of(WORLD_BASE + MEMBER_THIS), anything, anything, one_of(WORLD_BASE + MEMBER_A),
          one_of(WORLD_BASE + MEMBER_B)], presets=list_view_state, callees=LIST_VIEW_CALLEES),
    Spec("CListView_FillBackground", 0x004273f0, "fastcall",
         [one_of(WORLD_BASE + MEMBER_THIS), anything, anything, one_of(WORLD_BASE + MEMBER_A)],
         presets=list_view_state, callees=LIST_VIEW_CALLEES),
    Spec("CColumnVector_BaseEquivalent", 0x00427fc0, "stdcall", [one_of(WORLD_BASE + MEMBER_A), anything],
         callees=LIST_VIEW_CALLEES),
    Spec("WCIsvSListBase_ListDelete", 0x00428300, "stdcall", [one_of(0, WORLD_BASE + MEMBER_B, WORLD_BASE + MEMBER_B)],
         eax_mask=0),
]


# -- GameSettings::LoadSaveGame, SetupLocalPlayer and helpers (game_settings_load.cpp,
#    game_settings_local_player.cpp, the index checks in game_settings_vectors.cpp; session 58) --
# The settings object is the world (ECX = WORLD_BASE): teams at +0x3c,
# player setups at +0x58 (data block GS_SETUPS), net ids at +0x90; every
# vector's vtable is emu.FAKE_VTABLE (slot 1, offset 4: base_init_upto, the
# notification of an index check; slot 3, offset 0xc: the sorted find).
# Element copies, assignments and destructors are probed (verified on their
# own); a copy's source on the stack (a local of the function) is recorded
# as "stack".
GSL_TEAMS = 0x3c
GSL_PLAYERS = 0x58
GSL_NET_IDS = 0x90
GSL_TEAM_BLOCK = 0x28000
GSL_NET_BLOCK = 0x2c000


def gsl_stack_source(machine, ecx, args):
    return (_gs_stack(args[0]),)


GSL_ELEMENT_CALLEES = {
    0x0049eaf0: (1, "thiscall", "this", None, gsl_stack_source),  # GamePlayerSetup copy constructor
    0x0049edf0: (1, "thiscall", "this", None, gsl_stack_source),  # GamePlayerSetup::operator=
    0x0049ec80: (0, "thiscall", "probe"),                         # ~GamePlayerSetup
    0x00437fc0: (1, "thiscall", "probe"),                         # GamePlayerSetup scalar deleting destructor
    0x004a03a0: (1, "thiscall", "this", None, gsl_stack_source),  # CTeam copy constructor
    0x004a05c0: (1, "thiscall", "this", None, gsl_stack_source),  # CTeam::operator=
    0x00414b90: (1, "thiscall", "probe"),                         # CTeam scalar deleting destructor
    0x004a04d0: (0, "thiscall", "probe"),                         # ~CTeam
}


def gsl_vector(at: int, block: int, empty_chance: float = 0.3):
    """A vector at world `at` with FAKE_VTABLE; mostly small, sometimes empty
    (the growth of the index checks)."""
    def preset(rng, pool, args):
        if rng.random() < empty_chance:
            capacity = rng.choice([0, 0, 1, 2])
            data = WORLD_BASE + block if capacity else rng.choice([0, WORLD_BASE + block])
            return [(at, FAKE_VTABLE, 4), (at + 8, data, 4), (at + 0xc, capacity, 4),
                    (at + 0x10, rng.choice([0, 0, 1, -1]) & 0xFFFFFFFF, 4), (at + 0x14, 0, 4),
                    (at + 0x18, rng.choice([0, 1, 5]), 4)]
        length = rng.choice([1, 1, 2, 3, 4, 6, rng.randrange(1, 9)])
        capacity = length + rng.choice([0, 0, 1, 3])
        return [(at, FAKE_VTABLE, 4), (at + 8, WORLD_BASE + block, 4), (at + 0xc, capacity, 4),
                (at + 0x10, length, 4), (at + 0x14, length, 4), (at + 0x18, rng.choice([1, 5]), 4)]
    return preset


def gsl_setup_values(rng, count: int = 10, clan_values=(0, 0, 1, 2, 3)):
    """Small types, parties, clans and net ids for the first setups of the
    player block (the rest of each setup stays lazily random)."""
    fields = []
    for i in range(count):
        at = GS_SETUPS + i * GS_SETUP_BYTES
        fields += [(at + 0x5c, rng.choice([0, 0, 1, 2, 3, rng.getrandbits(32)]), 4),
                   (at + 0x14, rng.choice([0, 1, 2, 3, rng.randrange(8)]), 4),
                   (at + 0xb8, rng.choice(clan_values), 4),
                   (at + 0x10, rng.choice([0xFFFFFFFF, 0, 1, 2, 3, 4, rng.randrange(10)]), 4)]
    return fields


def gsl_team_ids(rng, team_block: int, count: int = 6):
    """Each team's player-id vector (+8) with FAKE_VTABLE and a few small ids."""
    fields = []
    for t in range(count):
        at = team_block + t * 0x40
        ids = rng.choice([0, 1, 2, 3])
        block = 0x2e000 + t * 0x100
        fields += [(at + 8, FAKE_VTABLE, 4), (at + 0x10, WORLD_BASE + block, 4), (at + 0x14, ids + 1, 4),
                   (at + 0x18, ids, 4), (at + 0x1c, ids, 4), (at + 0x20, 5, 4)]
        fields += [(block + 4 * k, rng.randrange(8), 4) for k in range(ids)]
    return fields


def gsl_settings(rng, pool, args):
    fields = gsl_vector(GSL_TEAMS, GSL_TEAM_BLOCK)(rng, pool, args)
    fields += gsl_vector(GSL_PLAYERS, GS_SETUPS)(rng, pool, args)
    fields += gsl_vector(GSL_NET_IDS, GSL_NET_BLOCK)(rng, pool, args)
    fields += [(0x28, rng.choice([0, 1]), 1), (0x2b, rng.choice([0, 1]), 1),
               (0xac, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]), 4)]
    fields += [(GSL_NET_BLOCK + 4 * k, rng.choice([0, 1, 2, 3, 4]), 4) for k in range(10)]
    fields += gsl_setup_values(rng) + gsl_team_ids(rng, GSL_TEAM_BLOCK)
    return fields


def gsl_clan(rng, pool):
    return rng.choice([0, 1, 2, 3, 4, rng.getrandbits(32)])


def gsl_net_player(rng, pool):
    return rng.choice([0, 0, WORLD_BASE + SCRATCH])


def gsl_player_setup_result(h):
    return WORLD_BASE + GS_SETUPS + (h % 6) * GS_SETUP_BYTES


GSL_PLAYER_SETUP_CALLEE = {0x004351e0: (1, "thiscall", gsl_player_setup_result)}  # g_gameSettings.PlayerSetup


def gsl_team(rng, pool, args):
    return gsl_team_ids(rng, 0, 1) + gsl_setup_values(rng, 6, (0, 0, 0, 1, 5, rng.getrandbits(32)))


def gsl_config(rng, pool, args):
    return [(0x158, rng.choice([0, 1, 2, 0xff]), 1)]


SETUP_LOCAL_CALLEES = {
    **GS_LOCK_CALLEES, **GSL_ELEMENT_CALLEES,
    0x00433a90: (1, "thiscall", "probe"),  # FindTeamForClan
    0x00433c80: (0, "thiscall", "probe"),  # LowestFreeNetId
    0x004a0e10: (1, "thiscall", "probe"),  # CTeam::CheckClans
    0x0049f360: (1, "thiscall", "probe"),  # GamePlayerSetup::SetType
    0x0049f190: (4, "thiscall", "probe"),  # GamePlayerSetup::AttachNetPlayer
    0x004a01c0: (0, "thiscall", "probe"),  # GamePlayerSetup::LogSlot
}

SPECS += [
    Spec("GamePlayerSetup_Clan", 0x004a01b0, "thiscall", []),
    # Dead blocks: the length clamp after the growth (0x437995, 0x437e65): the
    # length is 0 on that path.
    Spec("PlayerVector_Index", 0x00437950, "thiscall", [gv_index], presets=gsl_vector(0, GV_BLOCK, 0.4),
         callees=GSL_ELEMENT_CALLEES, virtuals={4: 1}, new_fails=0.1, exempt_blocks=(0x437995,)),
    Spec("TeamVector_Index", 0x00437e20, "thiscall", [gv_index], presets=gsl_vector(0, GV_BLOCK, 0.4),
         callees=GSL_ELEMENT_CALLEES, virtuals={4: 1}, new_fails=0.1, exempt_blocks=(0x437e65,)),
    Spec("PlayerVector_ResizeClampLength", 0x00438070, "thiscall", [gv_count], presets=gv_vector_fields(0, GV_BLOCK),
         callees=GSL_ELEMENT_CALLEES, new_fails=0.1),
    # The team at world 0, its players' setups from the probed
    # g_gameSettings.PlayerSetup (one of the first setups of GS_SETUPS).
    # Dead blocks (these and FindTeamForClan's): the inlined index checks
    # inside loops that run only while the index is below the length; an
    # out-of-range index needs a length of 0x80000001 or more (the signed
    # check), and such a loop would run 2^31 times. The same check is
    # covered by PlayerVector_Index / TeamVector_Index / PtrVector_Index.
    Spec("CTeam_FirstClan", 0x004a0cd0, "thiscall", [], presets=gsl_team, callees=GSL_PLAYER_SETUP_CALLEE,
         virtuals={4: 1}, new_fails=0.1, exempt_blocks=(0x4a0d05, 0x4a0d0d, 0x4a0d22, 0x4a0d2a, 0x4a0d2f, 0x4a0d3f, 0x4a0d4a, 0x4a0d50, 0x4a0d52, 0x4a0d56, 0x4a0d5e, 0x4a0d66, 0x4a0d87, 0x4a0d9a, 0x4a0d9f, 0x4a0daf, 0x4a0db3, 0x4a0db7, 0x4a0db9)),
    Spec("CTeam_CheckClans", 0x004a0e10, "thiscall", [gsl_clan], presets=gsl_team, callees=GSL_PLAYER_SETUP_CALLEE,
         virtuals={4: 1}, new_fails=0.1, exempt_blocks=(0x4a0e47, 0x4a0e4f, 0x4a0e64, 0x4a0e6c, 0x4a0e71, 0x4a0e81, 0x4a0e8c, 0x4a0e92, 0x4a0e94, 0x4a0e98, 0x4a0ea0, 0x4a0ea8, 0x4a0ec9, 0x4a0edc, 0x4a0ee1, 0x4a0ef1, 0x4a0ef5, 0x4a0ef9, 0x4a0efb)),
    Spec("GameSettings_FindTeamForClan", 0x00433a90, "thiscall", [gsl_clan], presets=gsl_settings,
         callees={**GSL_ELEMENT_CALLEES, 0x004a0cd0: (0, "thiscall", "probe")}, virtuals={4: 1}, new_fails=0.1,
         exempt_blocks=(0x433acd, 0x433ad1, 0x433ae6, 0x433af3, 0x433af7, 0x433b0b, 0x433b0f, 0x433b13, 0x433b15, 0x433b47, 0x433b4b, 0x433b60, 0x433b6d, 0x433b71, 0x433b85, 0x433b89, 0x433b8d, 0x433b8f, 0x433bf7, 0x433bfb, 0x433c15, 0x433c21, 0x433c26, 0x433c29, 0x433c39, 0x433c3d, 0x433c41, 0x433c45, 0x433c47)),
    # The settings' lock getters: AL (the rest of EAX is the lock's leftover).
    Spec("GameSettings_IsHost", 0x004318a0, "thiscall", [], callees=GS_LOCK_CALLEES, eax_mask=0xff),
    Spec("GameSettings_IsMultiplayer", 0x004318d0, "thiscall", [], callees=GS_LOCK_CALLEES, eax_mask=0xff),
    # Dead blocks: the second loop's inlined index check (as FirstClan's).
    Spec("GameSettings_LowestFreeNetId", 0x00433c80, "thiscall", [], presets=gsl_settings,
         callees={**GS_LOCK_CALLEES, **GSL_ELEMENT_CALLEES}, virtuals={4: 1}, new_fails=0.05,
         exempt_blocks=(0x433d7a, 0x433d7e, 0x433d98, 0x433da6, 0x433dac, 0x433db0, 0x433dc1, 0x433dc5, 0x433dc9,
                        0x433dcd, 0x433dcf)),
    # Dead blocks: the inlined index checks before AttachNetPlayer
    # (0x434034..0x434050, 0x4340b7..0x4340d7): SetType's access through
    # 0x437950 just before made the vector non-empty (or its growth failed,
    # and so does this one: operator new fails for the whole case), and the
    # slot is never negative; the LogSlot loop's inline growth
    # 0x434129..0x434248 (the loop runs only while the index is below the
    # length; see FirstClan).
    Spec("GameSettings_SetupLocalPlayer", 0x00433e50, "thiscall", [gsl_net_player, gsl_clan], presets=gsl_settings,
         callees=SETUP_LOCAL_CALLEES, virtuals={4: 1}, new_fails=0.1,
         exempt_blocks=(0x434034, 0x434039, 0x43403c, 0x434050, 0x4340b7, 0x4340bc, 0x4340bf, 0x4340d7, 0x434129,
                        0x434131, 0x43414a, 0x434152, 0x43415a, 0x434166, 0x43417d, 0x434193, 0x43419d, 0x4341ba,
                        0x4341c5, 0x4341d3, 0x4341dc, 0x4341de, 0x4341fa, 0x4341fe, 0x43421f, 0x434222, 0x43422a,
                        0x43423e, 0x434242, 0x434246, 0x434248)),
]


# SetType / AttachNetPlayer on the setup at world 0: the std::string +0x18
# (buffer in scratch slot 14), the 16-bit string +0x28 (slot 15); the
# 16-bit text at 0x7a8248 they assign is set per case (empty in the image).
# The net player at scratch slot 0: its name, a std::string at +0xc (buffer
# in slot 1). IsHost, the host notification 0x49fea0, the net player's data
# read 0x49fbc0 and size 0x5c0f70 (network code) are probed, as is
# assign_cstr_or_empty 0x413d70 (verified; its text is the static "").
SETUP_WIDE_TEXT = 0x007a8248
GSL_NET_PLAYER = SCRATCH


def gsl_wide_text(rng, pool, args):
    n = rng.choice([0, 0, 1, 2, 5, 31, 32, rng.randrange(40)])
    chars = [rng.randint(1, 0xffff) for _ in range(n)] + [0]
    return [(SETUP_WIDE_TEXT + 2 * k, c, 2) for k, c in enumerate(chars)] + null_string_count(rng, pool, args)


def gsl_setup_state(rng, pool, args):
    fields, _ = string_fields(rng, 0x18, SCRATCH + 14 * SCRATCH_SLOT_BYTES)
    wide, _ = wstring_fields(rng, 0x28, SCRATCH + 15 * SCRATCH_SLOT_BYTES)
    name, _ = string_fields(rng, GSL_NET_PLAYER + 0xc, SCRATCH + 1 * SCRATCH_SLOT_BYTES)
    fields += wide + name
    fields += [(0x5c, rng.choice([0, 1, 1, 2, 3, rng.getrandbits(32)]), 4), (0x68, rng.choice([0, 1]), 1),
               (0x00, rng.choice([0, WORLD_BASE + GSL_NET_PLAYER, rng.getrandbits(32)]), 4)]
    return fields


def gsl_type(rng, pool):
    return rng.choice([0, 1, 2, 3, 4, rng.getrandbits(32)])


def gsl_attach_net_player(rng, pool):
    return rng.choice([0, 0, WORLD_BASE + GSL_NET_PLAYER, WORLD_BASE + GSL_NET_PLAYER])


SETUP_STATE_CALLEES = {
    0x004318a0: (0, "thiscall", "probe"),  # GameSettings::IsHost (on g_gameSettings)
    0x0049fea0: (0, "thiscall", "probe"),  # the host's notification (network)
    0x00413d70: (1, "thiscall", "this"),   # std::string::assign_cstr_or_empty
    0x0049fbc0: (0, "thiscall", "probe"),  # reads the net player's data (network)
    0x005c0f70: (0, "thiscall", "probe"),  # the net player's data size
}

SPECS += [
    # Dead block: std::_Xlen 0x49f3ac (a text of more than 0x7ffffffd characters).
    Spec("GamePlayerSetup_SetType", 0x0049f360, "thiscall", [gsl_type], presets=gsl_setup_state,
         globals=gsl_wide_text, callees=SETUP_STATE_CALLEES, eax_mask=0, exempt_blocks=(0x49f3ac,)),
    # Dead blocks: the name's length clamped to npos (0x49f258: npos is
    # 0xffffffff) and the self-assignment (0x49f262: the net player's name
    # is not the setup's own string).
    Spec("GamePlayerSetup_AttachNetPlayer", 0x0049f190, "thiscall",
         [anything, anything, gsl_attach_net_player, lambda rng, pool: rng.choice([0, 1, 0x100, 0x101])],
         presets=gsl_setup_state, globals=gsl_wide_text, callees=SETUP_STATE_CALLEES,
         exempt_blocks=(0x49f258, 0x49f262)),
]


# LoadSaveGame: the CMapFile members are probed: the constructor (EAX =
# this; its vtable set to FAKE_VTABLE, slot 0 = the deleting destructor),
# Open, GetError (0 or negative), GetChunk (a chunk at a fixed world offset
# per type, or none), ReleaseChunk, Close; ThrowOutOfMemory and ThrowHResult
# end the run (the latter's string argument, a local, by its fields after
# the allocator byte). The vector members run (element copies probed); the
# CTeam and GamePlayerSetup constructors and destructors run;
# GameConfig's flag, Set, AddPlayerId, LogSlot and SetupLocalPlayer (each
# verified on its own) are probed.
GSL_CHUNK_AT = {0x00: 0x30000, 0x14: 0x31000, 0x1a: 0x32000, 0x1b: 0x33000, 0x1e: 0x34000}


def gsl_map_file_construct(machine, ecx, args, h):
    machine.guest_write(ecx, struct.pack("<I", FAKE_VTABLE))


def gsl_get_chunk(machine, ecx, args, h):
    at = GSL_CHUNK_AT.get(args[0])
    if at is None or h % 9 == 0:
        return 0
    return WORLD_BASE + at


def gsl_get_error(h):
    return (0, 0, 0, 0, 0xFFFFFFFF, 0x80004005)[h % 6]


def gsl_throw_hresult_record(machine, ecx, args):
    return (args[0], args[1], bytes(machine.guest_read(args[2] + 4, 12)))


LOAD_SAVE_CALLEES = {
    **GSL_ELEMENT_CALLEES,
    # The destructors also run on LoadSaveGame's locals (ECX on the stack):
    # recorded without ECX.
    0x0049ec80: (0, "stdcall", "probe"),                          # ~GamePlayerSetup
    0x004a04d0: (0, "stdcall", "probe"),                          # ~CTeam
    0x004a5a70: (1, "thiscall", "this", gsl_map_file_construct),  # Dirk::CMapFile::CMapFile
    0x004a5c80: (2, "thiscall", "probe"),                         # Open
    0x004a60d0: (0, "thiscall", gsl_get_error),                   # GetError
    0x004a60f0: (2, "thiscall", "probe", gsl_get_chunk),          # GetChunk
    0x004a6310: (2, "thiscall", "probe"),                         # ReleaseChunk
    0x004a5f40: (0, "thiscall", "probe"),                         # Close
    0x00610960: (1, "thiscall", "throw"),                         # Win32::ThrowOutOfMemory
    0x00611050: (3, "thiscall", "throw", None, gsl_throw_hresult_record),  # Win32::ThrowHResult
    0x00413bd0: (0, "thiscall", "probe"),                         # GameConfig::RandomRaceAmazons
    0x0049f120: (9, "thiscall", "probe"),                         # GamePlayerSetup::Set
    0x004a06d0: (1, "thiscall", "probe"),                         # CTeam::AddPlayerId
    0x004a01c0: (0, "thiscall", "probe"),                         # GamePlayerSetup::LogSlot
    0x00433e50: (2, "thiscall", "probe"),                         # SetupLocalPlayer
}


def gsl_load_settings(rng, pool, args):
    fields = gsl_vector(GSL_TEAMS, GSL_TEAM_BLOCK, 0.6)(rng, pool, args)
    fields += gsl_vector(GSL_PLAYERS, GS_SETUPS, 0.6)(rng, pool, args)
    fields += gsl_vector(GSL_NET_IDS, GSL_NET_BLOCK, 0.6)(rng, pool, args)
    for at in (GSL_TEAMS, GSL_PLAYERS, GSL_NET_IDS):  # nothing filled: Resize(0) frees only the block
        fields.append((at + 0x10, 0, 4))
        fields.append((at + 0x18, 5, 4))  # the game's growth step: with 0 the team loop never ends
    # The map name string (+0x2c): empty or a buffer in the world (a random
    # pointer could lead the original's refcount decrement into S3.EXE's
    # code, which the real game can't write).
    if rng.random() < 0.3:
        fields += [(0x2c + 4, 0, 4), (0x2c + 8, 0, 4), (0x2c + 0xc, 0, 4)]
    else:
        length = rng.randrange(40)
        fields += [(0x2c + 4, WORLD_BASE + 0x35001, 4), (0x2c + 8, length, 4),
                   (0x2c + 0xc, length + rng.randrange(40), 4), (0x35001 + length, 0, 1)]
    header = GSL_CHUNK_AT[0x00]
    name = rng.choice([0, 1, 5, 20, rng.randrange(60)])
    fields += [(header + 0x20 + k, rng.randint(1, 255), 1) for k in range(name)] + [(header + 0x20 + name, 0, 1)]
    players = GSL_CHUNK_AT[0x14]
    count = rng.choice([0, 1, 2, 3, 4, rng.randrange(9), rng.randrange(21), 0x80000000 | rng.getrandbits(8)])
    fields.append((players + 8, count, 4))
    for r in range(20):
        at = players + 0xc + 0x18 * r
        fields += [(at + 4, rng.randrange(8), 1), (at + 5, rng.choice([0, 1, 2, 3, rng.randrange(12)]), 1),
                   (at + 6, rng.choice([0, 0, 1, 2, 3, rng.randrange(12)]), 1),
                   (at + 7, rng.choice([0, 1, 2, 3, 4, rng.getrandbits(8)]), 1)]
    fields.append((GSL_CHUNK_AT[0x1e] + 8, rng.choice([0, 0, 1, 2]), 1))
    return fields


SPECS += [
    # Dead blocks (wip/logic-r4, after the first coverage run): the map name
    # from a null text 0x432c8b/0x432cab (the text lies inside chunk 0);
    # the cleanup after ThrowHResult 0x432f53..0x432fb3 (it never returns);
    # the index checks' out-of-range paths: teams 0x433193..0x4331e3 (the
    # party is below the length after the team loop), player setups
    # 0x433236..0x433281 and 0x4332ef..0x43332d (the setups were inserted
    # first, so the vector is not empty, and the slot is a byte, never
    # negative), net ids in the final loop 0x432d99..0x432ddb and
    # 0x4333e3..0x433441 (the loop runs only while the index is below the
    # length). LoadSaveGame was S3RE_HOOK_UNVERIFIED until this passed.
    Spec("GameSettings_LoadSaveGame", 0x00432b00, "thiscall",
         [lambda rng, pool: WORLD_BASE + SCRATCH, lambda rng, pool: rng.choice([0, 1, 0x100])],
         presets=gsl_load_settings, callees=LOAD_SAVE_CALLEES,
         # Only the map file's allocation fails (ThrowOutOfMemory): a failing
         # team insert makes the team loop run forever in both versions.
         virtuals={0: 1, 4: 1, 0x0c: (3, {0: 4, 2: 4})}, new_fails=0.05, new_fails_size=0x134,
         exempt_blocks=(0x432c8b, 0x432cab, 0x432d99, 0x432da1, 0x432dba, 0x432dc2, 0x432dc7, 0x432ddb,
                        0x432f53, 0x432f75, 0x432f77, 0x432f7b, 0x432fa5, 0x432faf, 0x432fb3,
                        0x433193, 0x433197, 0x4331b1, 0x4331bd, 0x4331c2, 0x4331c5, 0x4331d5, 0x4331d9,
                        0x4331dd, 0x4331e1, 0x4331e3, 0x433236, 0x43324b, 0x43325c, 0x433261, 0x433264,
                        0x433279, 0x433281, 0x4332ef, 0x433304, 0x433310, 0x433315, 0x433318, 0x433325,
                        0x43332d, 0x4333e3, 0x4333e9, 0x4333eb, 0x4333ef, 0x4333f7, 0x4333ff, 0x433420,
                        0x433423, 0x433437, 0x43343b, 0x43343f, 0x433441)),
]


# -- wip/ui-gdi: blits and GDI text of the UI controls (ui_gdi_members.cpp) --
# One region of the world for these specs (below SCRATCH): `this` of the
# member, the element its vbptr leads to, the vbtables, a parent element,
# the screen surface and its interface (FAKE_VTABLE: GetDC 0x44, ReleaseDC
# 0x68, the owner's restore 0x0c), the root screen (FAKE_VTABLE: AddChild
# 0x00; the control's element too: GetScreen 0x78), the rect argument and
# the tooltip text.
GDI_REGION = 0x1f40000
GDI_THIS = GDI_REGION + 0x1000
GDI_ELEMENT = GDI_REGION + 0x2000
GDI_PARENT = GDI_REGION + 0x2800
GDI_TABLE = GDI_REGION + 0x3000
GDI_SURFACE = GDI_REGION + 0x4000
GDI_IFACE = GDI_REGION + 0x4040
GDI_OWNER = GDI_REGION + 0x4080
GDI_SCREEN = GDI_REGION + 0x5000
GDI_SCREEN_ELEMENT = GDI_REGION + 0x5100
GDI_RECT = GDI_REGION + 0x6000
GDI_TEXT = GDI_REGION + 0x7000
GDI_RGB565 = (0xF800, 0x7E0, 0x1F)


def gdi_rect_value(rng):
    """Mostly an ordered rect on a screen, sometimes anything."""
    if rng.random() < 0.12:
        return [rng.getrandbits(32) for _ in range(4)]
    left, top = rng.randint(-20, 640), rng.randint(-20, 480)
    return [left & 0xFFFFFFFF, top & 0xFFFFFFFF, (left + rng.randint(-4, 300)) & 0xFFFFFFFF,
            (top + rng.randint(-4, 200)) & 0xFFFFFFFF]


def gdi_rect_fields(rng, offset):
    return [(offset + 4 * k, v, 4) for k, v in enumerate(gdi_rect_value(rng))]


def gdi_element_state(delta, client=None):
    """A FillBackground/Draw member: the vbptr at this + delta leading to
    the element (random flag1f, rect, parent null or set), the client rect
    at this + client, the rect argument."""
    def presets(rng, pool, args):
        out = [(GDI_THIS + delta, WORLD_BASE + GDI_TABLE, 4),
               (GDI_TABLE + 4, (GDI_ELEMENT - (GDI_THIS + delta)) & 0xFFFFFFFF, 4),
               (GDI_ELEMENT + 0x1f, rng.choice([0, 0, 1, 0x80]), 1),
               (GDI_ELEMENT + 0x18, rng.choice([0, WORLD_BASE + GDI_PARENT]), 4)]
        out += gdi_rect_fields(rng, GDI_ELEMENT + 8) + gdi_rect_fields(rng, GDI_RECT)
        if client is not None:
            out += gdi_rect_fields(rng, GDI_THIS + client)
        return out
    return presets


def gdi_progress_state(rng, pool, args):
    out = gdi_element_state(-0x108)(rng, pool, args)
    maximum = rng.choice([0, rng.randint(-5, 200), rng.randint(1, 200)])
    value = rng.randint(-10, 250)
    if rng.random() < 0.1:  # anything but INT_MIN / -1 (a divide fault in both)
        value, maximum = rng.getrandbits(32), rng.getrandbits(32) | 2
    return out + [(GDI_THIS - 0x8, maximum & 0xFFFFFFFF, 4), (GDI_THIS - 0xc, value & 0xFFFFFFFF, 4)]


def gdi_pixel_format(machine, ecx, args, h):
    """Dialog::PixelFormat(out): 8 dwords, half the time with the RGB565
    masks (sometimes one bit off)."""
    import struct
    words = [(h >> (k * 7)) & 0xFFFFFFFF for k in range(8)]
    if h & 1:
        words[4:7] = GDI_RGB565
        if (h >> 1) & 3 == 0:
            words[4 + (h >> 3) % 3] ^= 1 << ((h >> 5) & 31)
    machine.guest_write(args[0], struct.pack("<8I", *words))
    return args[0]


def gdi_rect_record(index):
    return lambda m, ecx, args: tuple(bytes(m.guest_read(a, 16)) if i == index else a for i, a in enumerate(args))


GDI_DRAW_CALLEES = {
    0x417460: (2, "thiscall", "probe", None, gdi_rect_record(1)),   # CScreenGroup::FillBackground
    0x417480: (3, "thiscall", "probe", None, gdi_rect_record(1)),   # CScreenGroup::Draw
    0x5bf0c0: (5, "thiscall", "probe", None,                         # CSurface::DDBlt
               lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)), args[1], args[2], args[3],
                                     bytes(m.guest_read(args[4], 4)), bytes(m.guest_read(args[4] + 0x50, 4)))),
    0x5bf130: (5, "thiscall", "probe"),                              # CSurface::DDBltFast
    0x416880: (1, "stdcall", "probe", gdi_pixel_format, lambda m, ecx, args: ()),  # Dialog::PixelFormat
    # The CRT's __RTtypeid and type_info::operator== (CListBox's parent
    # check): recorded, typeid's result is what operator== gets in ECX.
    0x61f911: (1, "cdecl", lambda h: 0x7E000000 | (h & 0xFFF0)),
    0x61f00c: (1, "thiscall", "probe"),
}

SPECS += [
    Spec("CStaticSurface_Draw", 0x0041df40, "fastcall",
         [one_of(WORLD_BASE + GDI_THIS), anything, anything, anything, anything],
         presets=lambda rng, pool, args: gdi_rect_fields(rng, GDI_THIS + 8), callees=GDI_DRAW_CALLEES),
    Spec("CTextView_FillBackground", 0x00421a20, "fastcall",
         [one_of(WORLD_BASE + GDI_THIS), anything, anything, one_of(WORLD_BASE + GDI_RECT)],
         presets=gdi_element_state(-0x430, -0x44), callees=GDI_DRAW_CALLEES),
    Spec("CListBox_FillBackground", 0x00420340, "fastcall",
         [one_of(WORLD_BASE + GDI_THIS), anything, anything, one_of(WORLD_BASE + GDI_RECT)],
         presets=gdi_element_state(-0x440, -0x48), callees=GDI_DRAW_CALLEES),
    Spec("CProgressBar_Draw", 0x004232f0, "fastcall",
         [one_of(WORLD_BASE + GDI_THIS), anything, anything, one_of(WORLD_BASE + GDI_RECT), anything],
         presets=gdi_progress_state, callees=GDI_DRAW_CALLEES),
]


def gdi_tooltip_state(rng, pool, args):
    """The control at GDI_THIS: vbptr +4 -> its element (FAKE_VTABLE), a
    tooltip or none, the tooltip text, the mouse; the screen's element (its
    vbptr at +4) with a screen-sized rect; the surface and its interface."""
    out = [(GDI_THIS + 4, WORLD_BASE + GDI_TABLE, 4),
           (GDI_TABLE + 4, (GDI_ELEMENT - (GDI_THIS + 4)) & 0xFFFFFFFF, 4),
           (GDI_ELEMENT, FAKE_VTABLE, 4),
           (GDI_THIS + 0x20, rng.choice([0, 0, 0, 0, 0, rng.getrandbits(32) | 1]), 4),
           (GDI_SCREEN, FAKE_VTABLE, 4), (GDI_SCREEN + 4, WORLD_BASE + GDI_TABLE + 0x40, 4),
           (GDI_TABLE + 0x44, (GDI_SCREEN_ELEMENT - (GDI_SCREEN + 4)) & 0xFFFFFFFF, 4),
           (GDI_SURFACE + 8, WORLD_BASE + GDI_IFACE, 4), (GDI_SURFACE + 0xc, WORLD_BASE + GDI_OWNER, 4),
           (GDI_IFACE, FAKE_VTABLE, 4), (GDI_OWNER, FAKE_VTABLE, 4)]
    fields, _ = string_fields(rng, GDI_THIS + 0x10, GDI_TEXT)
    out += fields
    screen = [0, 0, rng.choice([640, 800, 1024]), rng.choice([480, 600, 768])]
    if rng.random() < 0.1:
        screen = [rng.getrandbits(32) for _ in range(4)]
    out += [(GDI_SCREEN_ELEMENT + 8 + 4 * k, v & 0xFFFFFFFF, 4) for k, v in enumerate(screen)]
    for offset, low, high in ((0x28, -20, 1100), (0x2c, -40, 800)):
        value = rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(low, high) & 0xFFFFFFFF
        out.append((GDI_THIS + offset, value, 4))
    return out


def gdi_write_dc(machine, ecx, args, h):
    """IDirectDrawSurface::GetDC(iface, &dc): a device context."""
    import struct
    machine.guest_write(args[1], struct.pack("<I", 0xDC000000 | (h & 0xFFFF)))


SPECS += [
    # Exempt 0x41859c: the branch for a null "" literal (the compiler's
    # strlen guard on the constant 0x7a7868, never taken).
    Spec("CControl_ShowTooltip", 0x00418550, "fastcall", [one_of(WORLD_BASE + GDI_THIS), anything],
         presets=gdi_tooltip_state, new_fails=0.1, exempt_blocks=(0x0041859c,),
         virtuals={0x00: 1, 0x0c: 0, 0x78: (0, {}, {"returns": lambda h: WORLD_BASE + GDI_SCREEN}),
                   0x44: (2, {1: 0}, {"com": True, "returns": hresult_from}),
                   0x68: (2, {}, {"com": True, "returns": hresult_from})},
         virtual_effects={0x44: gdi_write_dc},
         callees={**EXCEPTION_CTORS,
                  0x416870: (0, "cdecl", lambda h: WORLD_BASE + GDI_SURFACE),      # Dialog::Surface
                  0x4168a0: (1, "stdcall", "probe"),                               # Dialog::Font
                  0x41c3e0: (3, "thiscall", "this", None, gdi_rect_record(0)),     # CStaticText::CStaticText
                  0x41ca60: (1, "thiscall", "probe"),                              # CStaticText::SetStyle
                  0x610960: (1, "thiscall", "throw")}),                            # Win32::ThrowOutOfMemory
]


# -- wip/ui-gdi: CButton, CVScrollBar, CDropdown members (ui_button_scroll.cpp).
# The object at BTN_OBJECT; its vbptr (+4) leads to its element (FAKE_VTABLE:
# Invalidate 0x1c, PostMessage 0x7c); functors and the dropdown's group use
# FAKE_VTABLE slot 0 (and RemoveChild 4). --
BTN_REGION = 0x1f50000
BTN_OBJECT = BTN_REGION + 0x400
BTN_TABLE = BTN_REGION + 0x2400
BTN_FUNCTORS = BTN_REGION + 0x2200
BTN_LIST_ELEMENT = BTN_REGION + 0x2600


def btn_small(rng):
    return rng.choice([0, 0, 1, rng.randint(2, 255)])


def btn_functor(rng):
    return rng.choice([0, WORLD_BASE + BTN_FUNCTORS, WORLD_BASE + BTN_FUNCTORS + 0x10])


def btn_common(element_offset):
    out = [(BTN_OBJECT + 4, WORLD_BASE + BTN_TABLE, 4),
           (BTN_TABLE + 4, (element_offset - 4) & 0xFFFFFFFF, 4),
           (BTN_OBJECT + element_offset, FAKE_VTABLE, 4),
           (BTN_FUNCTORS, FAKE_VTABLE, 4), (BTN_FUNCTORS + 0x10, FAKE_VTABLE, 4)]
    return out


def btn_button_state(commands):
    def presets(rng, pool, args):
        out = btn_common(0x50)
        out += [(BTN_OBJECT + 0x0c, btn_small(rng), 1), (BTN_OBJECT + 0x48, btn_small(rng), 1),
                (BTN_OBJECT + 0x49, btn_small(rng), 1), (BTN_OBJECT + 0x4a, btn_small(rng), 1),
                (BTN_OBJECT + 0x38, btn_functor(rng), 4), (BTN_OBJECT + 0x3c, btn_functor(rng), 4),
                (BTN_OBJECT + 0x40, btn_functor(rng), 4),
                (BTN_OBJECT + 0x44, rng.getrandbits(32) if commands and rng.random() < 0.6 else 0, 4)]
        return out
    return presets


def btn_scroll_state(rng, pool, args):
    out = btn_common(0x300)
    rng_range, page = rng.randint(0, 40), rng.randint(0, 12)
    position = rng.randint(-3, 40)
    if rng.random() < 0.1:
        rng_range, position = rng.getrandbits(32), rng.getrandbits(32)
    out += [(BTN_OBJECT + 0xb4, btn_functor(rng), 4), (BTN_OBJECT + 0x2e0, rng_range & 0xFFFFFFFF, 4),
            (BTN_OBJECT + 0x2e4, position & 0xFFFFFFFF, 4), (BTN_OBJECT + 0x2e8, page, 4)]
    return out


def btn_dropdown_state(rng, pool, args):
    out = btn_common(0x66c)
    out += [(BTN_OBJECT + 0x38, FAKE_VTABLE, 4),
            (BTN_OBJECT + 0x1cc, WORLD_BASE + BTN_TABLE + 0x10, 4),
            (BTN_TABLE + 0x14, (BTN_LIST_ELEMENT - (BTN_OBJECT + 0x1cc)) & 0xFFFFFFFF, 4),
            (BTN_OBJECT + 0x660, btn_small(rng), 1),
            (BTN_OBJECT + 0x664, rng.randint(-5, 300) & 0xFFFFFFFF, 4)]
    out += gdi_rect_fields(rng, BTN_OBJECT + 0x66c + 8)
    return out


def btn_open_dropdown(rng, pool, args):
    return [(0x7a8244, rng.choice([WORLD_BASE + BTN_OBJECT, 0, 0x12345678]), 4)]


def btn_small_arg(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.12 else rng.randint(-6, 45) & 0xFFFFFFFF


BTN_CALLEES = {
    0x4156e0: (1, "thiscall", "probe", None, lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)),)),  # SetRect
    0x415bd0: (0, "thiscall", "probe"),   # CScreenElement::KillFocus
    0x417480: (3, "thiscall", "probe"),   # CScreenGroup::Draw
    0x418380: (5, "thiscall", "probe"),   # CControl::OnLButtonDown
    0x4184e0: (5, "thiscall", "probe"),   # CControl::OnMouseLeave
}
BTN_VIRTUALS_1 = {0x00: 1, 0x04: 1, 0x1c: 0}
BTN_VIRTUALS_2 = {0x00: 2, 0x1c: 0}
BTN_THIS = one_of(WORLD_BASE + BTN_OBJECT)
BTN_ELEMENT = one_of(WORLD_BASE + BTN_OBJECT + 0x50)
BTN_DROP_ELEMENT = one_of(WORLD_BASE + BTN_OBJECT + 0x66c)
# Exempt blocks below: the original's null-object branches (`this ? element
# : 0` with `this` the object itself, never null).

SPECS += [
    Spec("CButton_IsPressed", 0x0041a990, "fastcall", [BTN_THIS, anything], presets=btn_button_state(False),
         eax_mask=0xFF),
    # Exempt 0x41a9ee: the command message (its other 15 bytes are an
    # uninitialized local of the original's frame; compared in-process).
    Spec("CButton_Click", 0x0041a9b0, "fastcall", [BTN_THIS, anything], presets=btn_button_state(False),
         virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES, exempt_blocks=(0x0041a9ee, 0x0041a9d4)),
    Spec("CButton_SetChecked", 0x0041aa20, "fastcall", [BTN_THIS, anything, below(3)],
         presets=btn_button_state(False), virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES, eax_mask=0xFF),
    Spec("CButton_KillFocus", 0x0041aa40, "fastcall", [BTN_ELEMENT, anything], presets=btn_button_state(False),
         virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES),
    Spec("CButton_OnLButtonDown", 0x0041aaa0, "fastcall", [BTN_ELEMENT, anything] + [anything] * 5,
         presets=btn_button_state(False), virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES, eax_mask=0xFF),
    Spec("CButton_OnLButtonUp", 0x0041ab10, "fastcall", [BTN_ELEMENT, anything] + [anything] * 5,
         presets=btn_button_state(False), virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES, eax_mask=0xFF),
    Spec("CButton_OnMouseLeave", 0x0041ab70, "fastcall", [BTN_ELEMENT, anything] + [anything] * 5,
         presets=btn_button_state(False), virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES, eax_mask=0xFF),
    Spec("CVScrollBar_ScrollBy", 0x0041e290, "fastcall", [BTN_THIS, anything, btn_small_arg],
         presets=btn_scroll_state, virtuals=BTN_VIRTUALS_2, exempt_blocks=(0x0041e2fa,)),
    # EAX: the caller's when the position doesn't change (every caller
    # overwrites it), so not compared.
    Spec("CVScrollBar_SetPosition", 0x0041e330, "fastcall", [BTN_THIS, anything, btn_small_arg],
         presets=btn_scroll_state, virtuals=BTN_VIRTUALS_2, eax_mask=0,
         exempt_blocks=(0x0041e353,)),
    Spec("CVScrollBar_SetRange", 0x0041e380, "fastcall",
         [BTN_THIS, anything, below(4), below(4), below(4)], presets=btn_scroll_state, virtuals=BTN_VIRTUALS_2,
         exempt_blocks=(0x0041e3ca,)),
    # GetPosition's body ends at 0x41e407; the button callback after it
    # (0x41e410) is not a listed function start.
    Spec("CVScrollBar_GetPosition", 0x0041e400, "fastcall", [BTN_THIS, anything], presets=btn_scroll_state,
         cover_until=0x0041e407),
    Spec("CDropdown_OnArrowClicked", 0x00422590, "fastcall", [BTN_THIS, anything, anything],
         presets=btn_dropdown_state, globals=btn_open_dropdown, virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES),
    Spec("CDropdown_OnFocusChanged", 0x00422770, "fastcall", [BTN_DROP_ELEMENT, anything, anything],
         presets=btn_dropdown_state, globals=btn_open_dropdown, virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES,
         eax_mask=0xFF),
    Spec("CDropdown_Draw", 0x004227d0, "fastcall", [BTN_DROP_ELEMENT, anything, anything, anything, anything],
         presets=btn_dropdown_state, globals=btn_open_dropdown, virtuals=BTN_VIRTUALS_1, callees=BTN_CALLEES),
]


# -- wip/menus-r5: CStaticText::SetText (ui_static_text.cpp), CUpDown --------
# (ui_updown.cpp)
#
# SetText: the label at world offset 0 (vtable = FAKE_VTABLE, Invalidate
# slot 0x1c probed), its text member at +0x28 (common.string_fields, buffer
# in scratch slot 0). The by-value argument's four dwords are the stack
# arguments: allocator, a buffer (none, its own in scratch slot 1, or the
# member's), length, capacity; the own buffer gets a count and a text of
# about the length.
SETTEXT_ARG_BUFFER = SCRATCH + SCRATCH_SLOT_BYTES


def settext_arg_ptr(rng, pool):
    return rng.choice([0, WORLD_BASE + SETTEXT_ARG_BUFFER + 1, WORLD_BASE + SETTEXT_ARG_BUFFER + 1,
                       WORLD_BASE + SCRATCH + 1])


def settext_length(rng, pool):
    return rng.choice([0, 1, 2, rng.randrange(40), rng.randrange(40)])


def settext_label(rng, pool, args):
    fields, ptr = string_fields(rng, 0x28, SCRATCH)
    out = [(0, FAKE_VTABLE, 4)] + fields
    if args[1] == WORLD_BASE + SCRATCH + 1 and ptr != args[1]:
        out += [(SCRATCH, rng.choice([0, 1, 0xff]), 1), (SCRATCH + 1, 0x41, 1), (SCRATCH + 2, 0, 1)]
    if args[1] == WORLD_BASE + SETTEXT_ARG_BUFFER + 1:
        length = args[2]
        text = length if rng.random() > 0.15 else rng.randrange(length + 4)
        out.append((SETTEXT_ARG_BUFFER, rng.choice([0, 0, 0, 1, 2, 0xfd, 0xfe, 0xff]), 1))
        out += [(SETTEXT_ARG_BUFFER + 1 + i, rng.randint(1, 255), 1) for i in range(text)]
        out.append((SETTEXT_ARG_BUFFER + 1 + text, 0, 1))
    return out


def settext_capacity(rng, pool):
    return rng.choice([0, 31, 40, 63, rng.randrange(80)])


# CUpDown: the object at world offset 0x400 (UICTL_OBJECT); its vbtables in
# scratch slot 6 (UICTL_VBTABLE: the up-down's at +0, the up button's at +8,
# the down button's at +0x10), the listener a fake object in slot 7.
# OnButtonClicked's sender is one of the two buttons' usual elements (or a
# random place, from random displacements) or another element.
UD_OBJ = UICTL_OBJECT
UD_UP_ELEMENT = UD_OBJ + 0x300
UD_DOWN_ELEMENT = UD_OBJ + 0x380


def ud_number(rng, pool):
    return rng.choice([0, 1, 2, 5, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x7FFFFFFE, rng.randrange(20),
                       rng.getrandbits(32)])


def ud_state(rng, pool, args):
    def number():
        return ud_number(rng, pool)
    lo = number()
    hi = (lo + rng.randrange(20)) & 0xFFFFFFFF if rng.random() < 0.5 else number()
    value = (lo + rng.randrange(20) - 2) & 0xFFFFFFFF if rng.random() < 0.5 else number()
    up = UD_UP_ELEMENT - (UD_OBJ + 0x100) if rng.random() < 0.8 else rng.randrange(0x400)
    down = UD_DOWN_ELEMENT - (UD_OBJ + 0x180) if rng.random() < 0.8 else rng.randrange(0x400)
    if rng.random() < 0.1:
        up = (UD_DOWN_ELEMENT - (UD_OBJ + 0x100)) & 0xFFFFFFFF  # both at the same element (up wins)
    return [(UD_OBJ + 4, WORLD_BASE + UICTL_VBTABLE, 4), (UICTL_VBTABLE + 4, 0x220 + 4 * rng.randrange(4), 4),
            (UD_OBJ + 0x100, WORLD_BASE + UICTL_VBTABLE + 8, 4), (UICTL_VBTABLE + 12, up, 4),
            (UD_OBJ + 0x180, WORLD_BASE + UICTL_VBTABLE + 16, 4), (UICTL_VBTABLE + 20, down, 4),
            (UD_OBJ + 0xb4, rng.choice([0, WORLD_BASE + UICTL_FAKES]), 4), (UICTL_FAKES, FAKE_VTABLE, 4),
            (UD_OBJ + 0x1fc, lo, 4), (UD_OBJ + 0x200, hi, 4), (UD_OBJ + 0x204, value, 4)]


def ud_sender(rng, pool):
    return rng.choice([WORLD_BASE + UD_UP_ELEMENT, WORLD_BASE + UD_DOWN_ELEMENT, WORLD_BASE + UD_UP_ELEMENT,
                       WORLD_BASE + UD_DOWN_ELEMENT, WORLD_BASE + UD_OBJ + 0x224])


def ud_settext_record(m, ecx, args):
    """SetText's argument without its allocator byte (stack garbage in the
    original), with its buffer's count byte and text."""
    ptr, length = args[1], args[2]
    contents = bytes(m.guest_read(ptr - 1, min(length, 40) + 2)) if ptr else b""
    return (ptr, length, args[3], contents)


UD_THIS = one_of(WORLD_BASE + UD_OBJ)
UD_SETVALUE_PROBE = {0x00422b90: (1, "thiscall", "probe")}

SPECS += [
    # Dead: 0x41c848 caps the length at npos (0xFFFFFFFF); 0x41c853..0x41c8a4
    # is assign()'s self-assignment branch (the argument is a copy on the
    # stack, never the member); 0x41c9bd the argument's ~string _Eos after
    # erase(0, npos) (_Grow(0) is false). The final _Tidy's decrement is in
    # 0x402170 (after the split the buffer is unshared or frozen).
    Spec("CStaticText_SetText", 0x0041c810, "thiscall",
         [anything, settext_arg_ptr, settext_length, settext_capacity], presets=settext_label,
         globals=null_string_count, virtuals={0x1c: 0},
         exempt_blocks=(0x0041c848, 0x0041c853, 0x0041c858, 0x0041c85d, 0x0041c86d, 0x0041c86f, 0x0041c873,
                        0x0041c89c, 0x0041c8a4, 0x0041c9bd)),
    Spec("CUpDown_GetValue", 0x00422b80, "fastcall", [UD_THIS, anything], presets=ud_state),
    # UpdateText probed (its own spec). Dead: 0x422bd0, the listener call
    # for a null up-down (this == 0).
    Spec("CUpDown_SetValue", 0x00422b90, "fastcall", [UD_THIS, anything, ud_number], presets=ud_state,
         virtuals={0: 1}, callees={0x00422c20: (0, "thiscall", "probe")}, exempt_blocks=(0x00422bd0,)),
    Spec("CUpDown_SetRange", 0x00422bf0, "fastcall", [UD_THIS, anything, ud_number, ud_number], presets=ud_state,
         callees=UD_SETVALUE_PROBE),
    # SetText probed, its argument recorded by contents. Dead: 0x422d19 and
    # 0x422d4a free the temporaries' buffer, which the argument still
    # shares (count 2, then 1: both releases only decrement).
    Spec("CUpDown_UpdateText", 0x00422c20, "fastcall", [UD_THIS, anything], presets=ud_state,
         callees={0x0041c810: (4, "thiscall", "probe", None, ud_settext_record)},
         exempt_blocks=(0x00422d19, 0x00422d4a)),
    Spec("CUpDown_OnButtonClicked", 0x00422e50, "fastcall", [UD_THIS, anything, ud_sender], presets=ud_state,
         callees={**UD_SETVALUE_PROBE, 0x006119b0: (1, "thiscall", "throw")}),
]


# -- wip/menus-r5: ui_group_members.cpp ---------------------------------------
# Objects at UICTL_OBJECT (world 0x400), vbtables in UICTL_VBTABLE (one per
# 8 bytes), fake objects and vtables as in the specs above. The children
# vectors' find (slot 0xc) gets stack pointers: recorded by contents.
GM_VIRTUALS = {0: 1, 4: 1, 0xc: (3, {0: 4, 2: 4})}


def gm_vbptr(at, slot, displacement):
    return [(at, WORLD_BASE + UICTL_VBTABLE + 8 * slot, 4), (UICTL_VBTABLE + 8 * slot + 4, displacement & 0xFFFFFFFF, 4)]


def gm_group_to_destroy(rng, pool, args):
    obj = UICTL_OBJECT
    out = gm_vbptr(obj + 4, 0, 0x7c + 4 * rng.randrange(4))
    data = rng.choice([0, WORLD_BASE + SCRATCH + 0x100, WORLD_BASE + SCRATCH + 0x100])
    length = 0 if data == 0 else rng.choice([0, 1, 2, 4, rng.randrange(6)])
    out += [(obj + 0xc + 8, data, 4), (obj + 0xc + 0xc, rng.choice([length, length + 1, 6]) if data else 0, 4),
            (obj + 0xc + 0x10, rng.randrange(-1, 6) & 0xFFFFFFFF, 4), (obj + 0xc + 0x14, length, 4)]
    for list_at in (0x34, 0x4c, 0x64):
        out.append((obj + list_at + 8, rng.choice([0, rng.getrandbits(32)]), 4))
    return out


def gm_remove_last_child(machine, ecx, args, h):
    """RemoveChild takes a child out: the vector's length - 1."""
    at = WORLD_BASE + UICTL_OBJECT + 0x20
    length = int.from_bytes(machine.guest_read(at, 4), "little")
    machine.guest_write(at, ((length - 1) & 0xFFFFFFFF).to_bytes(4, "little"))


def gm_group(rng, pool, args):
    obj = UICTL_OBJECT
    return [(obj, FAKE_VTABLE, 4), (obj + 0xc, FAKE_VTABLE, 4), (obj + 0x20, rng.choice([0, 1, 2]), 4)]


def gm_list_box(rng, pool, args):
    obj = UICTL_OBJECT
    visible = rng.choice([0, 1, 5, 9, rng.getrandbits(32)])
    count = rng.choice([visible, visible + 1, visible - 1, 0, rng.getrandbits(32)]) & 0xFFFFFFFF
    out = gm_vbptr(obj + 4, 0, 0x440 + 4 * rng.randrange(3)) + gm_vbptr(obj + 0xc4, 1, 0x200 + 4 * rng.randrange(3))
    out += [(obj + 0x38, FAKE_VTABLE, 4), (obj + 0x44, FAKE_VTABLE, 4), (obj + 0x58, rng.choice([0, 1, 2]), 4),
            (obj + 0x3f0, rng.choice([0, 1, 0x80]), 1), (obj + 0x3f1, rng.choice([0, 1, 0x80]), 1),
            (obj + 0x414, visible, 4), (obj + 0x434, count, 4),
            (obj + 0x3f4, rng.choice([1, 2, 13, 16, rng.randrange(1, 0x7FFFFFFF)]), 4)]
    return out


def gm_rect(rng, pool):
    return WORLD_BASE + SCRATCH + 0x200 + 16 * rng.randrange(4)


def gm_records(rng, pool, args):
    header = rng.choice([0, WORLD_BASE + SCRATCH + 0x300])
    cells = rng.choice([0, WORLD_BASE + SCRATCH + 0x400])
    displacement = 0x40 + 4 * rng.randrange(3)
    out = [(0x1c, header, 4), (0x10, cells, 4), (SCRATCH + 0x300, FAKE_VTABLE, 4)]
    out += gm_vbptr(SCRATCH + 0x404, 2, displacement) + [(SCRATCH + 0x404 + displacement, FAKE_VTABLE, 4)]
    return out


def gm_image_button(rng, pool, args):
    obj = UICTL_OBJECT
    displacement = 0x5c + 4 * rng.randrange(3)
    return gm_vbptr(obj + 4, 0, displacement) + [(obj + 4 + displacement, FAKE_VTABLE, 4),
                                                 (obj + 0x58, rng.choice([0xFFFFFFFF, 0, 0x2000a]), 4)]


def gm_dialog(rng, pool, args):
    obj = UICTL_OBJECT
    out = gm_vbptr(obj, 0, 0xc0 + 4 * rng.randrange(3))
    return out + [(obj + 0xc0 + 4 * k, FAKE_VTABLE, 4) for k in range(4)]


def gm_move_element(machine, ecx, args, h):
    """SetParent sometimes moves the dialog's element (its displacement + 4),
    to compare the re-read."""
    if h & 1:
        at = WORLD_BASE + UICTL_VBTABLE + 4
        displacement = int.from_bytes(machine.guest_read(at, 4), "little")
        machine.guest_write(at, (displacement + 4).to_bytes(4, "little"))


GM_OBJ = WORLD_BASE + UICTL_OBJECT

SPECS += [
    # The lists' clearAndDestroy / ~WCIsvListBase probed (their own specs;
    # the lists are random); RemoveChild 0x416fe0 probed, taking a child out.
    Spec("CScreenGroup_Destroy", 0x00416da0, "fastcall", [one_of(GM_OBJ + 0x80), anything],
         presets=gm_group_to_destroy,
         callees={0x00416fe0: (1, "thiscall", "probe", gm_remove_last_child),
                  0x0060b560: (0, "thiscall", "probe"), 0x0060b5a0: (0, "thiscall", "probe")}),
    Spec("CScreenGroup_ShowChild", 0x00417090, "fastcall", [one_of(GM_OBJ), anything, one_of(0, 1, 0x100, 0x80000000),
                                                            anything],
         presets=gm_group, virtuals=GM_VIRTUALS),
    # Dead: 0x41f561 and 0x41f5a6 (the scroll bar's element for a scroll bar
    # at address 0) and 0x41f629 (the same before AddChild). The body ends
    # at 0x41f6a1; 0x41f6b0 starts another member (not a listed start).
    Spec("CListBox_UpdateLayout", 0x0041f550, "fastcall", [one_of(GM_OBJ), anything], presets=gm_list_box,
         virtuals=GM_VIRTUALS, exempt_blocks=(0x0041f561, 0x0041f5a6, 0x0041f629), cover_until=0x0041f6a1),
    Spec("CListBox_Clear", 0x0041f1e0, "fastcall", [one_of(GM_OBJ), anything], presets=gm_list_box,
         callees={0x0040a580: (1, "thiscall", "probe"), 0x0041e380: (3, "thiscall", "probe"),
                  0x0041f550: (0, "thiscall", "probe")}),
    Spec("CListBox_OnResized", 0x004204d0, "fastcall", [one_of(GM_OBJ + 0x444), anything, gm_rect, anything],
         presets=gm_list_box, callees={0x0041f550: (0, "thiscall", "probe")}),
    Spec("CColumn_ConstructDefault", 0x004274a0, "thiscall", []),
    Spec("CLine_ConstructDefault", 0x004276b0, "thiscall", []),
    # EAX without a record is the caller's (unused).
    Spec("CColumn_Destroy", 0x004275e0, "thiscall", [], presets=gm_records, virtuals={0: 1}, eax_mask=0),
    Spec("CLine_Destroy", 0x004277d0, "thiscall", [], presets=gm_records, virtuals={0: 1}, eax_mask=0),
    Spec("CImageButton_OnMouseEnter", 0x0041acf0, "fastcall", [one_of(GM_OBJ + 0x60), anything] + [anything] * 5,
         presets=gm_image_button, virtuals={0x1c: 0}, callees={0x004184b0: (5, "thiscall", "probe")}),
    Spec("CImageButton_OnMouseLeave", 0x0041ad30, "fastcall", [one_of(GM_OBJ + 0x60), anything] + [anything] * 5,
         presets=gm_image_button, virtuals={0x1c: 0}, callees={0x0041ab70: (5, "thiscall", "probe")}),
    Spec("CDialog_SetParent", 0x00427d20, "fastcall", [one_of(GM_OBJ + 0xc0), anything, anything],
         presets=gm_dialog, virtuals={0xc: 1}, callees={0x004157d0: (1, "thiscall", "probe", gm_move_element)}),
]


# -- wip/menus-r5: ui_control_destructors.cpp ---------------------------------
# `this` at world 0x800; the destructors they call probed (their own
# specs); the strings they release from common.string_fields (buffers in
# scratch). Dead in each: the base pointer's null check (an object at
# address 0xb8) and, in the inlined ~string, the _Eos after erase(0, npos)
# where it is a call (_Grow(0) is false).
CD_THIS = 0x800
CD_CALLEES = {address: (0, "thiscall", "probe") for address in
              (0x00416da0, 0x004180a0, 0x0041a900, 0x004156b0, 0x0060b560, 0x0060b5a0)}


def cd_strings(*offsets):
    def presets(rng, pool, args):
        out = []
        for k, offset in enumerate(offsets):
            fields, _ = string_fields(rng, CD_THIS + offset, SCRATCH + k * SCRATCH_SLOT_BYTES)
            out += fields
        return out
    return presets


def cd_screen_list(rng, pool, args):
    return [(CD_THIS - 0x1c + 8, rng.choice([0, rng.getrandbits(32)]), 4)]


CD_OBJ = one_of(WORLD_BASE + CD_THIS)

SPECS += [
    Spec("CGroupControl_Destroy", 0x0041e210, "fastcall", [CD_OBJ, anything], callees=CD_CALLEES),
    Spec("CDialog_Destroy", 0x0042a0a0, "fastcall", [CD_OBJ, anything], callees=CD_CALLEES),
    Spec("CScreen_Destroy", 0x0042a750, "fastcall", [CD_OBJ, anything], presets=cd_screen_list, callees=CD_CALLEES),
    Spec("CVScrollBar_Destroy", 0x0041f010, "fastcall", [CD_OBJ, anything], callees=CD_CALLEES,
         exempt_blocks=(0x0041f0b4,)),
    Spec("CProgressBar_Destroy", 0x00429c70, "fastcall", [CD_OBJ, anything], presets=cd_strings(-0x58 + 0x28),
         callees=CD_CALLEES, exempt_blocks=(0x00429ced, 0x00429d2a)),
    Spec("CUpDown_Destroy", 0x0042b100, "fastcall", [CD_OBJ, anything], presets=cd_strings(-0x16c + 0x28),
         callees=CD_CALLEES, exempt_blocks=(0x0042b1ac, 0x0042b1e5)),
    Spec("CTextView_Destroy", 0x0042a380, "fastcall", [CD_OBJ, anything], presets=cd_strings(-0x24),
         callees=CD_CALLEES, exempt_blocks=(0x0042a3f6, 0x0042a491, 0x0042a4d4)),
]


# -- wip/menus-r5: ui_text_getters.cpp -----------------------------------------
# The object at world offset 0, its member from string_fields /
# wstring_fields (buffer in scratch slot 0); the result object in scratch
# slot 1 at one of a few addresses (GetText's allocator byte is its low
# byte), its bytes lazily random.
def tg_text_view(rng, pool, args):
    fields, _ = string_fields(rng, 0x410, SCRATCH)
    return fields


def tg_edit(rng, pool, args):
    fields, _ = wstring_fields(rng, 0x4c, SCRATCH)
    return fields


def tg_result(rng, pool):
    return WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + rng.choice([0, 4, 0x31, 0x80])


SPECS += [
    # Dead: 0x42078f caps the length at 0xFFFFFFFF; 0x420796..0x4207ea is
    # the self-assignment branch (the result is a new object).
    Spec("CTextView_GetText", 0x00420760, "fastcall", [one_of(WORLD_BASE), anything, tg_result],
         presets=tg_text_view, globals=null_string_count,
         exempt_blocks=(0x0042078f, 0x00420796, 0x004207a0, 0x004207a5, 0x004207b5, 0x004207b7, 0x004207bb,
                        0x004207e2, 0x004207ea)),
    # Dead: 0x418bef caps the length at npos (0xFFFFFFFF); 0x418bf5..0x418c44,
    # the self-assignment branch.
    Spec("CEditControl_GetTextW", 0x00418bc0, "fastcall", [one_of(WORLD_BASE), anything, tg_result],
         presets=tg_edit, globals=wstring_null_count,
         exempt_blocks=(0x00418bef, 0x00418bf5, 0x00418bfb, 0x00418c00, 0x00418c10, 0x00418c12, 0x00418c16, 0x00418c3c,
                        0x00418c44)),
]


# CEditControl::GetText (ui_text_getters.cpp): the object at world 0, its
# 16-bit text at +0x4c from framework's w2n_source pattern (moved to 0x4c);
# WideToNarrowString runs as original code in both (its own spec).
def eg_edit_text(rng, pool, args):
    return [(offset - 0x100 + 0x4c if offset < SCRATCH else offset, value, size)
            for offset, value, size in w2n_source(rng, pool, args)]


SPECS += [
    Spec("CEditControl_GetText", 0x00418a10, "fastcall", [one_of(WORLD_BASE), anything, tg_result],
         presets=eg_edit_text, globals=null_string_count, new_fails=0.05, callees=W2N_CALLEES,
         # Dead: the local's ~string: 0x418aed (_Eos after erase(0, npos))
         # and 0x418b0a/0x418b0e (the release's decrement: after the split
         # the buffer is unshared or frozen).
         exempt_blocks=(0x00418aed, 0x00418b0a, 0x00418b0e)),
]


# -- wip/dialog-r8 batch 1: small members of dialog.cpp (ui_dialog_setters.cpp)
#
# `this` at DLG_THIS (world offset; fields on both sides lazily random). An
# element part with the fake vtable sits at this (dlg_element) or behind a
# vbptr at this + delta (dlg_vb: vbtable in scratch slot 6, displacement
# 0x100). Every S3.EXE callee is probed (DLG_CALLEES); the allocator and
# the member-function pointer of NewLink / CFunctor2::Invoke point at
# trivial S3.EXE functions used as probe addresses (DLG_ALLOC_PROBE: 0
# arguments, ECX recorded; DLG_METHOD_PROBE: 2 arguments).
DLG_THIS = MEMBER_THIS
DLG_OTHER = MEMBER_A
DLG_LINKS = MEMBER_B
DLG_ALLOC_PROBE = 0x004289d0   # UI_sub_4289d0 (mov eax, 7FFFFFFDh; ret), not called by these functions
DLG_METHOD_PROBE = 0x0041abd0  # Win32::CMessageQueue::vf02 (ret 0Ch), not called by these functions


def dlg_pointer_or_null(h):
    return 0 if h % 4 == 0 else WORLD_BASE + DLG_LINKS + 4 * ((h >> 8) % 8)


def dlg_pointer(h):
    return WORLD_BASE + DLG_LINKS + 4 * ((h >> 8) % 8)


def dlg_check_result(h):
    return DDERR_SURFACELOST if h % 3 == 0 else hresult_from(h >> 2)


DLG_CALLEES = {
    0x416190: (2, "thiscall", "probe"), 0x4168c0: (0, "stdcall", "probe"), 0x5faaa0: (2, "thiscall", "probe"),
    0x413d70: (1, "thiscall", "probe"), 0x420eb0: (0, "thiscall", "probe"), 0x41e330: (1, "thiscall", "probe"),
    0x418810: (1, "thiscall", "probe"), 0x425b50: (0, "thiscall", "probe"), 0x4242a0: (1, "thiscall", "probe"),
    0x4243b0: (1, "thiscall", "probe"), 0x4252c0: (0, "thiscall", "probe"), 0x4258f0: (0, "thiscall", "probe"),
    0x60bab0: (1, "thiscall", dlg_pointer_or_null), 0x60bbe0: (1, "thiscall", dlg_pointer),
    0x429a80: (1, "thiscall", "probe"), 0x429b50: (1, "thiscall", "probe"),
    0x5bf2e0: (2, "thiscall", dlg_check_result), 0x6119b0: (1, "thiscall", "throw"),
    DLG_ALLOC_PROBE: (0, "thiscall", dlg_pointer_or_null), DLG_METHOD_PROBE: (2, "thiscall", "probe"),
}
DLG_VIRTUALS = {0x00: 1, 0x1c: 0, 0x7c: 5}


def dlg_vb(delta, at=DLG_THIS, displacement=0x100):
    """A vbptr at `at` + delta whose virtual base (with the fake vtable) is
    `displacement` bytes after it."""
    return [(at + delta, WORLD_BASE + MEMBER_VBTABLE, 4), (MEMBER_VBTABLE + 4, displacement, 4),
            (at + delta + displacement, FAKE_VTABLE, 4)]


def dlg_presets(*parts):
    """Presets from fixed lists and callables (rng -> list)."""
    def presets(rng, pool, args):
        out = []
        for part in parts:
            out += part(rng) if callable(part) else part
        return out
    return presets


def dlg_spec(name, address, args, presets=None, **kwargs):
    kwargs.setdefault("callees", DLG_CALLEES)
    kwargs.setdefault("virtuals", DLG_VIRTUALS)
    return Spec(name, address, "fastcall", [one_of(WORLD_BASE + DLG_THIS), anything, *args],
                presets=presets, **kwargs)


def dlg_small_or_any(rng, pool):
    return rng.choice([0, 1, 2, 3, rng.getrandbits(32)])


def dlg_count(offset):
    return lambda rng: [(DLG_THIS + offset, rng.randint(-2, 5) & 0xFFFFFFFF, 4)]


def dlg_field_choice(offset, *values):
    return lambda rng: [(DLG_THIS + offset, rng.choice(values), 4)]


DLG_ELEMENT = [(DLG_THIS, FAKE_VTABLE, 4)]
DLG_PARENT = [WORLD_BASE + DLG_OTHER] * 3 + [0]

SPECS += [
    dlg_spec("CScreen_GetScreen", 0x00417f90, []),
    dlg_spec("CButton_IsChecked", 0x0041a9a0, [], eax_mask=0xFF),
    dlg_spec("CStaticSurface_GetSurface", 0x0041df30, []),
    dlg_spec("CListBox_GetItems", 0x0041f240, []),
    dlg_spec("CListBox_GetSelection", 0x0041f3a0, []),
    dlg_spec("CListView_GetFullyVisibleLineCount", 0x00424570, []),
    dlg_spec("CListView_GetLineCount", 0x00424580, []),
    dlg_spec("CListView_GetDefaultLineHeight", 0x004245b0, []),
    dlg_spec("CImageButton_SetGfxFile", 0x0041acb0, [anything]),
    dlg_spec("CStaticText_SetFont", 0x0041ca50, [anything]),
    dlg_spec("CColumn_SetTooltipTextId", 0x004276a0, [anything]),
    dlg_spec("CDialog_SetDefaultButton", 0x00427920, [anything]),
    dlg_spec("CDialog_SetCancelButton", 0x00427930, [anything]),
    dlg_spec("CStaticText_SetHighlight", 0x0041ca20, [anything], presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CStaticText_SetColor", 0x0041ca30, [anything], presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CStaticImage_SetImage", 0x0041d7c0, [anything], presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CStaticFrame_SetFrame", 0x0041d9c0, [anything, anything], presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CStaticColorIndexedFrame_SetFrame", 0x0041dbc0, [anything] * 3, presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CImageButton_SetImages", 0x0041acc0, [anything] * 4, presets=dlg_presets(dlg_vb(4))),
    dlg_spec("CTextButton_SetStyle", 0x0041b960, [anything], presets=dlg_presets(dlg_vb(4))),
    dlg_spec("CEditControl_OnFocusChanged", 0x00419b10, [anything], presets=dlg_presets(dlg_vb(-0x6c))),
    dlg_spec("CScreen_InitDialogs", 0x00417f30, [anything, anything], presets=dlg_presets(dlg_vb(4))),
    dlg_spec("CTextView_SetMaxLines", 0x00420d40, [dlg_small_or_any],
             presets=dlg_presets(dlg_field_choice(0x420, 0, 1, 2, 3))),
    dlg_spec("CTextView_Clear", 0x00420730, []),
    dlg_spec("CTextView_ScrollTo", 0x00420d60, [anything], presets=dlg_presets(dlg_vb(4))),
    dlg_spec("CListView_ScrollTo", 0x00425110, [anything], presets=dlg_presets(dlg_vb(4))),
    dlg_spec("CStaticText_SetTextId", 0x0041c9f0, [anything], presets=dlg_presets(DLG_ELEMENT)),
    dlg_spec("CDropdown_SetEnabled", 0x00422460, [dlg_small_or_any],
             presets=dlg_presets([(DLG_THIS + 0x1c8, FAKE_VTABLE, 4)])),
    # 0x41a57d passes a null `this` to the functor: dead, `this` was just
    # dereferenced (+0x3c).
    dlg_spec("CEditControl_NotifyRejectedChar", 0x0041a570, [], exempt_blocks=(0x0041a57d,),
             presets=dlg_presets(dlg_vb(4), dlg_field_choice(0x3c, 0, WORLD_BASE + DLG_OTHER),
                                 [(DLG_OTHER, FAKE_VTABLE, 4)])),
    # The bound object at DLG_OTHER: its vbptr (+4) -> a vbtable with small
    # entries; the method a probe; the vbtable index 0 or 4.
    dlg_spec("CFunctor2_Invoke", 0x004281a0, [anything, anything],
             presets=dlg_presets(lambda rng: [
                 (DLG_THIS + 4, WORLD_BASE + DLG_OTHER, 4), (DLG_OTHER + 4, WORLD_BASE + MEMBER_VBTABLE, 4),
                 (MEMBER_VBTABLE, rng.randint(-0x80, 0x80) & 0xFFFFFFFF, 4),
                 (MEMBER_VBTABLE + 4, rng.randint(-0x80, 0x80) & 0xFFFFFFFF, 4),
                 (DLG_THIS + 8, DLG_METHOD_PROBE, 4), (DLG_THIS + 0xc, rng.randint(-0x40, 0x40) & 0xFFFFFFFF, 4),
                 (DLG_THIS + 0x10, rng.choice([0, 4]), 4)])),
    dlg_spec("CListView_OnResized", 0x004268c0, [anything, anything]),
    dlg_spec("CListView_Clear", 0x00423a20, [], presets=dlg_presets(dlg_count(0xdc), dlg_count(0xf8))),
    dlg_spec("CListView_RemoveAllLines", 0x00423a70, [], presets=dlg_presets(dlg_count(0xf8))),
    # 0x424e92: base_insert failed (only out of memory; find just failed, so
    # no duplicate): reads address 0.
    dlg_spec("CListView_GetCell", 0x00424e60, [anything, anything], exempt_blocks=(0x00424e92,)),
    dlg_spec("WCSkipListBase_insert", 0x00428180, [anything]),
    dlg_spec("WCValOrderedVector_CColumn_resize", 0x00429940, [dlg_small_or_any],
             presets=dlg_presets(dlg_field_choice(0x14, 0, 1, 2, 3, 4, 5))),
    dlg_spec("WCValOrderedVector_CLine_resize", 0x00429960, [dlg_small_or_any],
             presets=dlg_presets(dlg_field_choice(0x14, 0, 1, 2, 3, 4, 5))),
    dlg_spec("WCValSList_bool_NewLink", 0x004298d0, [one_of(WORLD_BASE + DLG_OTHER, WORLD_BASE + DLG_OTHER + 3)],
             presets=dlg_presets(dlg_field_choice(0x10, 0, DLG_ALLOC_PROBE)), new_fails=0.3),
    Spec("DirectX_CSurface_DDGetDC", 0x00419ae0, "thiscall", [anything], presets=surface_objects,
         virtuals={0x44: (2, {}, {"com": True, "returns": hresult_from})}, callees=DLG_CALLEES),
    Spec("CScreenElement_ReleaseDC", 0x00416080, "fastcall", [anything, anything, one_of(WORLD_BASE), anything],
         presets=surface_objects, virtuals={0x68: (2, {}, {"com": True, "returns": hresult_from})},
         callees=DLG_CALLEES),
    dlg_spec("CScreenElement_PostMessage", 0x004160f0, [anything] * 5,
             presets=dlg_presets(dlg_vb(4, at=DLG_OTHER), dlg_field_choice(0x18, *DLG_PARENT))),
]


# -- wip/dialog-r8 batch 2: list selection and cell getters
# (ui_list_selection.cpp), the screen queue and PopModal (ui_screen_queue.cpp).
# Same objects as batch 1. The selection fields small, -1 or near the top
# line, sometimes huge (the end of the visible range wraps). The vector's
# find (slot 3) writes the index through its third argument (a stack local,
# recorded by its contents). The queue links (from get(0), DLG_LINKS) hold
# 0 or one of two elements with the fake vtable (their low bytes 0 and 1,
# for the bool list).
DLG_LIST_CALLEES = dict(DLG_CALLEES)
DLG_LIST_CALLEES.update({0x61f9f0: (5, "cdecl", "probe"), 0x426f60: (1, "thiscall", "probe"),
                         0x425110: (1, "thiscall", "probe"), 0x420d60: (1, "thiscall", "probe"),
                         0x417f50: (0, "thiscall", "probe"),
                         0x60b500: (1, "thiscall", dlg_pointer_or_null), 0x415850: (5, "thiscall", "probe")})
DLG_SELECT_CALLEES = dict(DLG_LIST_CALLEES)
DLG_SELECT_CALLEES[0x41f3b0] = (2, "thiscall", "probe")  # CListBox::SetSelection (not for its own spec)
DLG_QUEUE_CALLEES = dict(DLG_LIST_CALLEES)
DLG_QUEUE_CALLEES[0x60b500] = (1, "thiscall", dlg_pointer)
DLG_FOCUS = [0, WORLD_BASE + DLG_OTHER, WORLD_BASE + DLG_OTHER + 0x101]


def dlg_selection(selection, top, visible):
    def part(rng):
        t = rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-3, 8)
        v = rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-2, 5)
        s = -1 if rng.random() < 0.15 else rng.getrandbits(32) if rng.random() < 0.1 else t + rng.randint(-4, 8)
        return [(DLG_THIS + top, t & 0xFFFFFFFF, 4), (DLG_THIS + visible, v & 0xFFFFFFFF, 4),
                (DLG_THIS + selection, s & 0xFFFFFFFF, 4)]
    return part


def dlg_find_index(machine, ecx, args, h):
    machine.guest_write(args[2], ((h >> 16) % 5).to_bytes(4, "little"))


def dlg_flag_byte(rng, pool):
    return rng.choice([0, 1, 0x100, rng.getrandbits(32)])


def dlg_focus_links(rng):
    return [(DLG_LINKS + 4 * k, rng.choice(DLG_FOCUS), 4) for k in range(10)] + \
           [(DLG_OTHER, FAKE_VTABLE, 4), (DLG_OTHER + 0x101, FAKE_VTABLE, 4)]


SPECS += [
    *[dlg_spec(name, address, [dlg_small_or_any, dlg_small_or_any], callees=DLG_LIST_CALLEES)
      for name, address in (("CListView_GetCellEdit", 0x00424eb0), ("CListView_GetCellStaticText", 0x00424f10),
                            ("CListView_GetCellImageButton", 0x00424f70), ("CListView_GetCellStaticFrame", 0x00424fd0),
                            ("CListView_GetCellUpDown", 0x00425030))],
    dlg_spec("CListView_SelectLine", 0x00425090, [dlg_small_or_any, dlg_flag_byte], callees=DLG_LIST_CALLEES,
             presets=dlg_presets(dlg_vb(4), dlg_selection(0x194, 0x190, 0x188))),
    # The list box takes the selection from its argument (stored first).
    dlg_spec("CListBox_SetSelection", 0x0041f3b0,
             [lambda rng, pool: rng.choice([0xFFFFFFFF, rng.getrandbits(32)] + list(range(-4, 10))) & 0xFFFFFFFF,
              dlg_flag_byte], callees=DLG_LIST_CALLEES,
             presets=dlg_presets(dlg_vb(4), lambda rng: [(DLG_THIS + 0x410, rng.choice([0, 1, 3, 0x7FFFFFFF]), 4),
                                                         (DLG_THIS + 0x414, rng.choice([0, 1, 2, 3, 5, 0x7FFFFFFF]), 4)])),
    dlg_spec("CListBox_SelectString", 0x0041f4f0, [anything, dlg_flag_byte], callees=DLG_SELECT_CALLEES,
             virtuals={0x0c: (3, {2: 4})}, virtual_effects={0x0c: dlg_find_index},
             presets=dlg_presets([(DLG_THIS + 0x420, FAKE_VTABLE, 4)], dlg_field_choice(0x434, 0, 0, 1, 3))),
    # 0x417e12: HasQueuedMessages was true but the queue had no link: an
    # uninitialized message would be dispatched (can't happen).
    dlg_spec("CScreen_ProcessQueuedMessage", 0x00417df0, [], callees=DLG_QUEUE_CALLEES, exempt_blocks=(0x00417e12,),
             virtuals={0x04: 1}, presets=dlg_presets(dlg_vb(4), [(DLG_THIS + 0x7c, FAKE_VTABLE, 4)])),
    dlg_spec("CScreen_OnMessage", 0x00417e70, [anything] * 5, callees=DLG_LIST_CALLEES, virtuals={0x04: 1},
             presets=dlg_presets(dlg_vb(-0x94), [(DLG_THIS - 0x1c, FAKE_VTABLE, 4)])),
    dlg_spec("CScreenGroup_PopModal", 0x00417a30, [anything], callees=DLG_LIST_CALLEES, virtuals={0x04: 1, 0x0c: 1},
             presets=dlg_presets([(DLG_THIS + o, FAKE_VTABLE, 4) for o in (0, 0x34, 0x4c, 0x64)], dlg_focus_links)),
]


# -- wip/dialog-r8 batch 3: CListView cell setters by column and the scroll
# bar layout (ui_list_view_cells.cpp). The column vector at this + 0xc8
# (fake vtable; its data in scratch, column types 5..8; capacity and length
# small, often 0); the line count small. UpdateScrollBar: the element and
# the bar's element through vbptrs +4 / +0x19c (one vbtable, displacement
# 0x100); the counts and the top line small or huge. SetRect's rect is a
# stack local: recorded by its contents.
DLG_COLUMNS = SCRATCH + 7 * SCRATCH_SLOT_BYTES
DLG_CELL_CALLEES = dict(DLG_LIST_CALLEES)
DLG_CELL_CALLEES.update({0x422bf0: (2, "thiscall", "probe"), 0x422b90: (1, "thiscall", "probe"),
                         0x423160: (2, "thiscall", "probe"), 0x4276a0: (1, "thiscall", "probe"),
                         0x418330: (1, "thiscall", "probe"), 0x41e380: (3, "thiscall", "probe"),
                         0x4156e0: (1, "thiscall", "probe", None, lambda m, ecx, args: (bytes(m.guest_read(args[0], 16)),)),
                         0x417090: (2, "thiscall", "probe")})
DLG_SCROLL_CALLEES = dict(DLG_CELL_CALLEES)
del DLG_SCROLL_CALLEES[0x425b50]  # UpdateScrollBar itself
# Dead: the inlined operator[] clamps the length to 1 after growing an
# empty vector (its length is 0 there, and the bare resize leaves it).
DLG_CLAMP_DEAD = {0x00424c70: (0x00424cc2,), 0x00424d70: (0x00424dc2,), 0x00423fe0: (0x00424037,)}


def dlg_columns(rng):
    out = [(DLG_THIS + 0xc8, FAKE_VTABLE, 4), (DLG_THIS + 0xc8 + 8, WORLD_BASE + DLG_COLUMNS, 4),
           (DLG_THIS + 0xc8 + 0xc, rng.choice([0, 0, 1, 3, 8]), 4),
           (DLG_THIS + 0xc8 + 0x14, rng.choice([0, 0, 1, 2, 5, 8]), 4),
           (DLG_THIS + 0xf8, rng.randrange(5), 4)]
    out += [(DLG_COLUMNS + 0x20 * k + 4, rng.randint(5, 8), 4) for k in range(8)]
    return out


def dlg_column_index(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-2, 9) & 0xFFFFFFFF


def dlg_scroll_counts(rng):
    def count():
        return rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(12)
    top = rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-2, 3) & 0xFFFFFFFF
    return [(DLG_THIS + 0xf8, count(), 4), (DLG_THIS + 0x18c, count(), 4), (DLG_THIS + 0x190, top, 4),
            (DLG_THIS + 0x4f2, rng.randrange(3), 1)]


SPECS += [
    dlg_spec("CListView_SetCellUpDown", 0x00424c70, exempt_blocks=DLG_CLAMP_DEAD[0x00424c70], args=[dlg_small_or_any, dlg_column_index, anything, anything, anything],
             callees=DLG_CELL_CALLEES, virtuals={0x04: 1}, eax_mask=0, presets=dlg_presets(dlg_columns)),
    dlg_spec("CListView_SetCellProgress", 0x00424d70, exempt_blocks=DLG_CLAMP_DEAD[0x00424d70], args=[dlg_small_or_any, dlg_column_index, anything, anything],
             callees=DLG_CELL_CALLEES, virtuals={0x04: 1}, eax_mask=0, presets=dlg_presets(dlg_columns)),
    dlg_spec("CListView_SetColumnTooltip", 0x00423fe0, exempt_blocks=DLG_CLAMP_DEAD[0x00423fe0], args=[dlg_column_index, anything],
             callees=DLG_CELL_CALLEES, virtuals={0x04: 1}, eax_mask=0, presets=dlg_presets(dlg_columns)),
    # 0x425c72: the scroll bar's address (this + 0x198) tested for null: dead.
    dlg_spec("CListView_UpdateScrollBar", 0x00425b50, [], callees=DLG_SCROLL_CALLEES, exempt_blocks=(0x00425c72,),
             presets=dlg_presets(dlg_vb(4), [(DLG_THIS + 0x19c, WORLD_BASE + MEMBER_VBTABLE, 4)], dlg_scroll_counts)),
]


# -- wip/dialog-r8 batch 4: text view layout, list box clicks
# (ui_text_view_layout.cpp). Counts small or huge, line heights never 0 (a
# division), rects small; OnResized's rect at DLG_OTHER. The list box's
# listener none or an object with the fake vtable (slot 0: 2 arguments).
DLG_TEXT_CALLEES = dict(DLG_CELL_CALLEES)
DLG_TEXT_CALLEES.update({0x420d90: (0, "thiscall", "probe"), 0x418380: (5, "thiscall", "probe")})
DLG_UPDATE_CALLEES = dict(DLG_TEXT_CALLEES)
del DLG_UPDATE_CALLEES[0x420d90]  # UpdateLayout itself


def dlg_small_count(rng):
    return (rng.getrandbits(31) if rng.random() < 0.1 else rng.randint(-3, 14)) & 0xFFFFFFFF


def dlg_line_height(rng):
    return (-rng.randint(1, 9) if rng.random() < 0.12 else rng.randint(1, 16)) & 0xFFFFFFFF


def dlg_text_view(at):
    def part(rng):
        return [(at + o, dlg_small_count(rng), 4) for o in (0x400, 0x404, 0x408)] + \
               [(at + 0x40c, dlg_line_height(rng), 4), (at + 0xc4, WORLD_BASE + MEMBER_VBTABLE, 4)]
    return part


def dlg_rect_at(at):
    return lambda rng: [(at + 4 * k, rng.randint(-20, 200) & 0xFFFFFFFF, 4) for k in range(4)]


def dlg_click_position(rng, pool):
    return (rng.randint(-5, 120) & 0xFFFF) | ((rng.randint(-5, 120) & 0xFFFF) << 16)


def dlg_list_box(rng):
    rect = [rng.randint(-5, 20), rng.randint(-5, 20), rng.randint(20, 120), rng.randint(20, 120)]
    return [(DLG_THIS - 0x48 + 4 * k, v & 0xFFFFFFFF, 4) for k, v in enumerate(rect)] + [
        (DLG_THIS - 0x438, rng.choice([0, 0, 0, 1]), 1), (DLG_THIS - 0x50, dlg_line_height(rng), 4),
        (DLG_THIS - 0x34, rng.randint(-3, 10) & 0xFFFFFFFF, 4), (DLG_THIS - 0x10, rng.randrange(20), 4),
        (DLG_THIS - 0x390, rng.choice([0, WORLD_BASE + DLG_OTHER, WORLD_BASE + DLG_OTHER]), 4),
        (DLG_OTHER, FAKE_VTABLE, 4)]


SPECS += [
    # 0x420df1, 0x420e50: the scroll bar (a member, this + 0xc0) tested for null: dead.
    dlg_spec("CTextView_UpdateLayout", 0x00420d90, [], callees=DLG_UPDATE_CALLEES,
             exempt_blocks=(0x00420df1, 0x00420e50), presets=dlg_presets(dlg_vb(4), dlg_text_view(DLG_THIS))),
    dlg_spec("CTextView_OnResized", 0x00421bb0, [one_of(WORLD_BASE + DLG_OTHER), anything], callees=DLG_TEXT_CALLEES,
             presets=dlg_presets(dlg_text_view(DLG_THIS - 0x434), dlg_rect_at(DLG_OTHER),
                                 [(MEMBER_VBTABLE + 4, 0x100, 4)])),
    # 0x420480: the list box's own address (this - 0x444) tested for null: dead.
    dlg_spec("CListBox_OnLButtonDown", 0x004203f0, [anything, anything, dlg_click_position, anything, anything],
             callees=DLG_TEXT_CALLEES, virtuals={0x00: 2, 0x1c: 0}, exempt_blocks=(0x00420480,),
             presets=dlg_presets(dlg_vb(-0x440), dlg_list_box)),
]


# -- wip/dialog-r8 batch 5: LayoutColumns (ui_list_view_cells.cpp), the
# scroll bar's click and the animated button's timer (ui_control_input.cpp).
# The scroll bar's track sometimes 0 high (x87 division by zero: inf/NaN
# through _ftol); OnTimer's states -1..5 (out-of-range ones index the
# sequence ids without a check), its jump table (0x41b2a0) after the code.
DLG_INPUT_CALLEES = dict(DLG_TEXT_CALLEES)
DLG_INPUT_CALLEES.update({0x427650: (3, "thiscall", "probe"), 0x41a990: (0, "thiscall", "probe"),
                          0x41a9a0: (0, "thiscall", "probe"), 0x5fad20: (1, "thiscall", "probe")})


def dlg_layout_columns(rng):
    out = dlg_columns(rng)
    out += [(DLG_COLUMNS + 0x20 * k + 8, rng.randint(-5, 60) & 0xFFFFFFFF, 4) for k in range(8)]
    out += [(DLG_THIS + 0x4e0, rng.randint(-10, 50) & 0xFFFFFFFF, 4),
            (DLG_THIS + 0x4e8, rng.randint(-10, 200) & 0xFFFFFFFF, 4)]
    return out


def dlg_scroll_track(rng):
    top = rng.randint(-5, 60)
    bottom = top if rng.random() < 0.1 else top + rng.randint(-3, 160)

    def value():
        return rng.getrandbits(32) if rng.random() < 0.12 else rng.randint(-3, 40) & 0xFFFFFFFF
    track = [rng.randint(-5, 20), top, rng.randint(20, 120), bottom]
    return [(DLG_THIS - 0x24 + 4 * k, v & 0xFFFFFFFF, 4) for k, v in enumerate(track)] + [
        (DLG_THIS - 0x14, 0 if rng.random() < 0.12 else value(), 4), (DLG_THIS - 0x0c, value(), 4),
        (DLG_THIS - 0x10, rng.randrange(20), 4),
        (DLG_THIS - 0x240, rng.choice([0, WORLD_BASE + DLG_OTHER, WORLD_BASE + DLG_OTHER]), 4),
        (DLG_OTHER, FAKE_VTABLE, 4)]


def dlg_track_click(rng, pool):
    return (rng.randint(-5, 120) & 0xFFFF) | ((rng.randint(-5, 220) & 0xFFFF) << 16)


def dlg_animated_button(rng):
    out = [(DLG_THIS - 0x1c, rng.randint(-1, 5) & 0xFFFFFFFF, 4),
           (DLG_THIS - 0x18, 0x80000000 if rng.random() < 0.1 else rng.randint(-2, 3) & 0xFFFFFFFF, 4)]
    out += [(DLG_THIS - 0x14 + 4 * k, 0xFFFFFFFF if rng.random() < 0.35 else rng.randrange(50), 4)
            for k in range(-1, 6)]
    return out


SPECS += [
    dlg_spec("CListView_LayoutColumns", 0x00425150, [], callees=DLG_INPUT_CALLEES, virtuals={0x04: 1}, eax_mask=0,
             # Dead: the column index runs below the column count, which is
             # the vector's length, so both operator[] checks never fail (all
             # their out-of-range and empty-vector blocks).
             exempt_blocks=(0x42519d, 0x4251a1, 0x4251bb, 0x4251c7, 0x4251cc, 0x4251cf, 0x4251df, 0x4251e3, 0x4251e7,
                            0x4251eb, 0x4251ed, 0x42521a, 0x42521e, 0x425238, 0x425244, 0x425249, 0x42524c, 0x42525c,
                            0x425260, 0x425264, 0x425268, 0x42526a),
             presets=dlg_presets(dlg_vb(4), dlg_layout_columns)),
    dlg_spec("CVScrollBar_OnLButtonDown", 0x0041e470, [anything, anything, dlg_track_click, anything, anything],
             callees=DLG_INPUT_CALLEES, virtuals={0x00: 2, 0x1c: 0},
             presets=dlg_presets(dlg_vb(-0x2f0), dlg_scroll_track)),
    dlg_spec("CAnimatedButton_OnTimer", 0x0041b1b0, [], callees=DLG_INPUT_CALLEES, cover_until=0x0041b2a0,
             presets=dlg_presets(dlg_vb(-0x64), dlg_animated_button)),
]


# -- wip/dialog-r8 batch 6: CEditControl::SetText, CDropdown::OnListSelect
# (ui_dropdown_text.cpp). NarrowToWideString (fastcall, no stack arguments)
# is probed: it fills the caller's local 16-bit string (ECX) with no buffer
# or one whose count byte is 0, 0xff or shared, and returns the local
# (recorded by its contents at SetTextW: a stack address); its source (EDX)
# is recorded. The dropdown's list box
# items (from the GetItems probe) are a string vector in scratch.
DLG_WIDE_BUFFER = DLG_LINKS + 0x12


def dlg_widen(machine, ecx, args, h):
    from unicorn.x86_const import UC_X86_REG_ECX
    local = machine.uc.reg_read(UC_X86_REG_ECX)
    choice = h % 6
    words = [h & 0xFF, 0 if choice == 0 else WORLD_BASE + DLG_WIDE_BUFFER, (h >> 8) % 9, 0]
    machine.guest_write(local, b"".join(w.to_bytes(4, "little") for w in words))
    if choice:
        machine.guest_write(WORLD_BASE + DLG_WIDE_BUFFER - 1, bytes([[0, 0xFF, 1, 2, 0xFE][choice - 1]]))
    return local  # the result object, as NarrowToWideString returns it


def dlg_widen_source(machine, ecx, args):
    from unicorn.x86_const import UC_X86_REG_EDX
    return (machine.uc.reg_read(UC_X86_REG_EDX),)


DLG_STRINGS = SCRATCH + 7 * SCRATCH_SLOT_BYTES + 0x800
DLG_DROPDOWN_CALLEES = dict(DLG_INPUT_CALLEES)
DLG_DROPDOWN_CALLEES.update({0x60b840: (0, "stdcall", "probe", dlg_widen, dlg_widen_source),
                             0x418d40: (1, "thiscall", "probe", None,
                                        lambda m, ecx, args: (ecx, bytes(m.guest_read(args[0], 16)))),
                             0x41f240: (0, "thiscall", lambda h: WORLD_BASE + DLG_STRINGS),
                             0x40a580: (1, "thiscall", "probe"), 0x418b40: (1, "thiscall", "probe")})
DLG_SETTEXT_CALLEES = dict(DLG_DROPDOWN_CALLEES)
del DLG_SETTEXT_CALLEES[0x418b40]  # SetText itself


def dlg_dropdown(rng):
    return [(DLG_THIS + 0x38, FAKE_VTABLE, 4), (DLG_THIS + 0x1cc, WORLD_BASE + MEMBER_VBTABLE, 4),
            (DLG_THIS + 4, WORLD_BASE + MEMBER_VBTABLE, 4), (MEMBER_VBTABLE + 4, 0x100, 4),
            (DLG_THIS + 0xb4, rng.choice([0, WORLD_BASE + DLG_OTHER, WORLD_BASE + DLG_OTHER]), 4),
            (DLG_OTHER, FAKE_VTABLE, 4),
            (DLG_STRINGS, FAKE_VTABLE, 4), (DLG_STRINGS + 8, WORLD_BASE + DLG_STRINGS + 0x40, 4),
            (DLG_STRINGS + 0xc, rng.choice([0, 0, 1, 4]), 4), (DLG_STRINGS + 0x14, rng.choice([0, 0, 1, 2, 5]), 4)]


SPECS += [
    dlg_spec("CEditControl_SetText", 0x00418b40, [anything], callees=DLG_SETTEXT_CALLEES),
    # Dead: 0x422680 and 0x42273d test the list box member and `this` for
    # null; 0x4226f1 clamps the length after growing an empty vector
    # (DLG_CLAMP_DEAD).
    dlg_spec("CDropdown_OnListSelect", 0x00422670, [anything, dlg_column_index], callees=DLG_DROPDOWN_CALLEES,
             virtuals={0x00: 2, 0x04: 1}, exempt_blocks=(0x00422680, 0x0042273d, 0x004226f1),
             presets=dlg_presets(dlg_dropdown)),
]
