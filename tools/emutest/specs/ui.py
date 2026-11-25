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
    Spec("GamePlayerSetup_Colour", 0x0049fec0, "fastcall", [scratch(0)], max_cases=200),
]


# -- GameSettings::MapName (game_settings.cpp, session 55) ---------------------
# ECX = the world as the settings object: its map name (+0x2c) any kind of
# string (common.string_fields: empty, consistent, odd, shared counts,
# frozen); the result a scratch string, sometimes the map name itself.
def map_name_settings(rng, pool, args):
    fields, _ = string_fields(rng, 0x2c, STRING_SELF_BUFFER)
    return fields


def null_string_count(rng, pool, args):
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
         presets=map_name_settings, globals=null_string_count,
         exempt_blocks=(0x436a8c, 0x436a9d, 0x436ab8, 0x436adf)),
]
