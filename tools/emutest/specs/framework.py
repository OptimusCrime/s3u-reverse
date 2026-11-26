"""Emulator specs, part `framework`: specs of the replacements in src/s3re/game/framework/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def session_desc(rng, pool, args):
    """The DirectPlay record at STRING_OTHER: its session name (+0x30) is the
    C text or null."""
    fields = member_strings(())(rng, pool, args)
    fields.append((STRING_OTHER + 0x30, rng.choice([STRING_TEXT_PTR, STRING_TEXT_PTR, 0]), 4))
    return fields


BY_VALUE_BUFFER = SCRATCH + 6 * SCRATCH_SLOT_BYTES


def by_value_string(rng, pool, args):
    """The buffer of the string passed by value (args 0..3): count byte, text,
    sometimes a zero earlier or later than its length."""
    fields = member_strings(())(rng, pool, args)
    fields.append((BY_VALUE_BUFFER, rng.choice([0, 0, 1, 2, 0xfd, 0xfe, 0xff]), 1))
    text = args[2] if rng.random() < 0.8 else rng.randrange(0x66)
    for i in range(text):
        fields.append((BY_VALUE_BUFFER + 1 + i, rng.randint(1, 255), 1))
    fields.append((BY_VALUE_BUFFER + 1 + text, 0, 1))
    return fields

SPECS += [
    # DESTROY: the blocks at +0xbc (_Eos) and +0xdd (decrement) of each.
    Spec(name, address, "thiscall", [], eax_mask=0, presets=member_strings(offsets),
         exempt_blocks=(address + 0xbc, address + 0xdd) if len(offsets) == 1 else
         (0x005e1bb5, 0x005e1bd5, 0x005e1c60, 0x005e1c7d))
    for name, address, offsets in (
        ("DestroyStringMember14_411980", 0x00411980, (0x14,)),
        ("DestroyStringMember08_441740", 0x00441740, (0x08,)),
        ("DestroyStringMember14_450ae0", 0x00450ae0, (0x14,)),
        ("DestroyStringMember14_455170", 0x00455170, (0x14,)),
        ("DestroyStringMember04_4581a0", 0x004581a0, (0x04,)),
        ("DestroyStringMember04_459440", 0x00459440, (0x04,)),
        ("DestroyStringMember50_45f060", 0x0045f060, (0x50,)),
        ("DestroyStringMember04_499470", 0x00499470, (0x04,)),
        ("DestroyStringMember04_49a070", 0x0049a070, (0x04,)),
        ("DestroyStringMembers10And00_5e1b10", 0x005e1b10, (0x10, 0x00)),
    )
]

SPECS += [
    Spec("SetStringMember14_412130", 0x00412130, "thiscall",
         [one_of(WORLD_BASE + 0x14, STRING_OTHER_PTR + 0x14, STRING_OTHER_PTR + 0x14)], eax_mask=0,
         presets=member_strings((0x14,), (0x14,)), globals=null_string_count,
         exempt_blocks=(0x0041214a, 0x00412155)),  # NPOS
    Spec("SetStringMember10_4181f0", 0x004181f0, "thiscall",
         [one_of(WORLD_BASE + 0x10, STRING_OTHER_PTR + 0x10, STRING_OTHER_PTR + 0x10)], eax_mask=0,
         presets=member_strings((0x10,), (0x10,)), globals=null_string_count,
         exempt_blocks=(0x0041820a, 0x00418215)),  # NPOS
    Spec("AssignTwoDwordsAndString_4499e0", 0x004499e0, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x08,), (0x08,)), globals=null_string_count,
         exempt_blocks=(0x00449a0e, 0x00449a1d)),  # NPOS
    Spec("SessionDesc_Assign_5be000", 0x005be000, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x50,), (0x50,)), globals=null_string_count,
         exempt_blocks=(0x005be02f, 0x005be03a)),  # NPOS
    # Dead: 0x5ed5d8 (NPOS) and the whole self-assignment path from 0x5ed5de
    # on: the function returns at once when this == x, so this + 0x14 ==
    # x + 0x14 never holds.
    Spec("LobbyRecord_Assign_5ed590", 0x005ed590, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x14,), (0x14,)), globals=null_string_count,
         exempt_blocks=(0x005ed5d8, 0x005ed5de, 0x005ed5e7, 0x005ed5ec, 0x005ed5fc, 0x005ed5fe,
                        0x005ed602, 0x005ed62a, 0x005ed632)),
    Spec("SessionDesc_Construct_5bb0b0", 0x005bb0b0, "thiscall",
         [one_of(0, STRING_OTHER_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR)], presets=session_desc,
         globals=empty_text),
    Spec("CDirectDrawDevice_Construct", 0x005b6790, "thiscall",
         [one_of(0, STRING_OTHER_PTR), text_or_null, text_or_null], presets=member_strings(()),
         exempt_blocks=(0x005b689a, 0x005b6978)),  # DROP (the first temporary)
    Spec("CRegistryValueInfo_Construct", 0x006161e0, "thiscall",
         [below(256), one_of(0, WORLD_BASE + BY_VALUE_BUFFER + 1, WORLD_BASE + BY_VALUE_BUFFER + 1),
          below(0x60), below(0x80), anything], presets=by_value_string,
         exempt_blocks=(0x006162ab, 0x006162cb)),  # DESTROY (of the parameter)
]


# -- batch 23: framework constructors (win32_classes.cpp) ------------------
SPECS += [
    Spec("Win32_CObject_Construct", 0x00610320, "thiscall", []),
    # Ghidra's function also takes in the unrelated code at 0x610340.
    Spec("Win32_CObject_Destroy", 0x00610330, "thiscall", [], eax_mask=0, cover_until=0x00610340),
    Spec("Win32_CObject_DeletingDestroy", 0x0060bfc0, "thiscall", [string_bool]),
    Spec("Win32_CWin32Handle_Construct", 0x0060be60, "thiscall", [anything, anything]),
    Spec("Win32_CWaitable_Construct", 0x0060bf50, "thiscall", [anything, anything]),
    Spec("Win32_CWindowBase_Construct", 0x0060f960, "thiscall", []),
    Spec("Win32_CMessageQueue_Construct", 0x0060d690, "thiscall", [anything]),
    Spec("DirectX_CSimpleSurface_Construct", 0x005befb0, "thiscall", []),
    Spec("DirectX_CSimpleSurface_Construct4", 0x005befe0, "thiscall",
         [anything, anything, anything, anything]),
    Spec("DirectX_CDirectSoundBuffer_Construct", 0x005c1f70, "thiscall", []),
]

SPECS += [
    Spec("DirectX_CSurface_Construct", 0x005bf200, "thiscall", []),
]


def equal_ids(rng, pool, args):
    value = rng.getrandbits(32)
    other = value if rng.random() < 0.5 else value ^ (1 << rng.randrange(32))
    return [(4, value, 4), (SCRATCH + 4, other, 4)]

SPECS += [
    Spec("GameSettings_GameGuid", 0x00437400, "thiscall", [scratch(0)]),
    Spec("Watcom_CopyByte", 0x004584f0, "stdcall", [scratch(0), scratch(1)]),
    Spec("Lobby_EqualField4", 0x00606ac0, "thiscall", [scratch(0)], presets=equal_ids),
    # The C runtime's free (0x621b7e) locks the heap and calls HeapFree: probed.
    Spec("png_free", 0x005aa0a0, "stdcall", [one_of(0, WORLD_BASE + SCRATCH), one_of(0, WORLD_BASE + SCRATCH + 0x100)],
         callees={0x00621b7e: (1, "cdecl", "probe")}),
]


# -- batch 26: framework helpers (framework_helpers.cpp) ---------------------
#
# Objects at world offset 0 (ECX = the world). Freed pointers point into
# scratch (operator delete is recorded by the fake heap). CloseHandle is an
# API probe (emu.API_HANDLERS).

NET_WIDE = SCRATCH + 12 * SCRATCH_SLOT_BYTES
NET_VECTOR = SCRATCH + 13 * SCRATCH_SLOT_BYTES


def pointer_or_null(offset: int, target: int):
    return lambda rng, pool, args: [(offset, rng.choice([0, WORLD_BASE + target]), 4)]


def net_destroy_world(rng, pool, args):
    fields = member_strings((0x18, 0xbc))(rng, pool, args)
    fields.append((0x40, rng.choice([0, WORLD_BASE + NET_VECTOR]), 4))
    wide = rng.choice([0, WORLD_BASE + NET_WIDE + 2])
    fields.append((0x2c, wide, 4))
    fields.append((NET_WIDE + 1, rng.choice([0, 0, 1, 2, 0xfe, 0xff]), 1))
    return fields

SPECS += [
    Spec("Net_DeletePointee_49d980", 0x0049d980, "thiscall", [], eax_mask=0,
         presets=pointer_or_null(0, SCRATCH)),
    Spec("NetSession_SetPair_5bd200", 0x005bd200, "thiscall", [anything, anything]),
    Spec("Lobby_ZeroThreeDwords_5dc810", 0x005dc810, "thiscall", []),
    Spec("Lobby_CPlayerIndex_Construct", 0x006065f0, "thiscall", [anything]),
    Spec("Lobby_CMemoryBlock_Construct", 0x00600e00, "thiscall", []),
    Spec("Lobby_ZeroSixDwords_5ec1e0", 0x005ec1e0, "thiscall", []),
    Spec("Credits_TextStyle_5a0960", 0x005a0960, "thiscall", []),
    Spec("Win32_CFile_Construct", 0x006195c0, "thiscall", []),
    Spec("NetSession_Init_5bb040", 0x005bb040, "thiscall", []),
    Spec("Lobby_Init_5ebfb0", 0x005ebfb0, "thiscall", []),
    Spec("Lobby_FreeBlock_601250", 0x00601250, "thiscall", [], presets=pointer_or_null(4, SCRATCH)),
    Spec("DirectX_CPlayer_Clear", 0x005c0ef0, "thiscall", [], presets=member_strings((0x0c, 0x1c))),
    Spec("Win32_CWin32Handle_Close", 0x0060bf20, "thiscall", [], presets=handle_world),
    Spec("Win32_CThread_Construct", 0x0060df70, "thiscall", [anything, anything]),
    # DEAD 0x49ecf7 / 0x49edbf: the inlined erase() of ~string removes every
    # character, so its _Grow(0, false) is false and the _Eos after it never
    # runs (as in String_Destroy).
    Spec("GamePlayerSetup_Destruct", 0x0049ec80, "thiscall", [], eax_mask=0, presets=net_destroy_world,
         exempt_blocks=(0x0049ecf7, 0x0049edbf)),
]


# -- batch 26: exception classes (win32_exceptions.cpp) -----------------------
#
# The constructors build the object at world offset 0 (its message string
# at +8) from the string in scratch slot 0 (string_world's second string;
# sometimes the object's own message member, the self-assignment path).
# ThrowDataError gets two strings from member_strings (STRING_OTHER and
# STRING_OTHER + 0x10) and throws through _CxxThrowException.


def message_ptr(rng, pool) -> int:
    return WORLD_BASE + 8 if rng.random() < 0.1 else STRING_OTHER_PTR


def mostly_derived(rng, pool) -> int:
    return rng.choice([0, 1, rng.getrandbits(32)])

SPECS += [
    # DEAD: 0x611a7d caps a length above 0xFFFFFFFF (`cmp ebx, -1; jbe`);
    # 0x611a8e, 0x611aa9 and 0x611ad1 are the _Xran, erase-memmove and _Eos
    # of the inlined self-assignment, whose length the constructor has just
    # set to 0 (the message member is cleared before assign() reads it).
    Spec("Win32_CException_Construct", 0x00611a20, "thiscall", [message_ptr, mostly_derived], presets=string_world,
         exempt_blocks=(0x00611a7d, 0x00611a8e, 0x00611aa9, 0x00611ad1)),
    Spec("Win32_CRunTimeError_Construct", 0x00614c60, "thiscall", [message_ptr, mostly_derived], presets=string_world),
    Spec("Lobby_CDataError_Construct", 0x006003b0, "thiscall", [message_ptr, mostly_derived], presets=string_world),
    Spec("Lobby_ThrowDataError", 0x005ffcc0, "fastcall",
         [one_of(STRING_OTHER_PTR), one_of(STRING_OTHER_PTR + 0x10), anything],
         presets=member_strings((), (0x0, 0x10)), new_fails=0.2, new_fails_size=0x18),
]

SPECS += [
    Spec("Win32_CProgrammingError_Construct", 0x00614eb0, "thiscall", [message_ptr, mostly_derived],
         presets=string_world),
    # Dead: 0x612700 is the _Eos after erase(0, npos) of a temporary; the
    # new length is always 0, for which _Grow returns false.
    Spec("Win32_COutOfMemory_Construct", 0x006124a0, "thiscall", [message_ptr, mostly_derived],
         presets=string_world, exempt_blocks=(0x00612700,)),
    # ignore_bytes: the result's allocator byte is the low byte of the
    # address of the original's last stack temporary (0x408cc0 takes it from
    # its argument's slot), ours that of our own temporary; MSVC 6.0's
    # allocator is an empty class that is never read.
    Spec("Win32_CAssertmentFailed_Describe", 0x00612e80, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=assertment_object, ignore_bytes=(STRING_RESULT,)),
    # Dead: 0x612b72 and 0x612c2e are the _Eos after erase(0, npos) (as
    # above); 0x612bcd / 0x612bd1 split the text from 0x612e80 (called
    # directly) when its buffer is shared, but that function frees the
    # temporary its result shared with, so the count is always 0.
    Spec("Win32_CAssertmentFailed_Construct", 0x00612a20, "thiscall",
         [text_or_null, text_or_null, anything, mostly_derived], presets=string_world, globals=empty_text,
         exempt_blocks=(0x00612b72, 0x00612bcd, 0x00612bd1, 0x00612c2e)),
    Spec("Win32_ThrowAssertmentFailed", 0x006119b0, "fastcall", [text_or_null, text_or_null, anything],
         presets=string_world, globals=empty_text, new_fails=0.2, new_fails_size=0x24),
    Spec("Win32_ThrowOutOfMemory", 0x00610960, "fastcall", [text_or_null, text_or_null, anything],
         presets=string_world, new_fails=0.2, new_fails_size=0x18),
]


# -- batch 27: small framework, container, UI and graphics members
# (framework_small.cpp) and string::compare(const char*) -------------------
#
# Objects at world offset 0; nodes, outputs and data in scratch slots.


def field_or_zero(offset: int, target: int):
    return lambda rng, pool, args: [(offset, rng.choice([0, WORLD_BASE + target, rng.getrandbits(32)]), 4)]


def gfx_tables(table_field: int):
    def presets(rng, pool, args):
        return [(table_field + 4 * g, WORLD_BASE + SCRATCH + (1 + g) * SCRATCH_SLOT_BYTES, 4) for g in range(4)]
    return presets


def gfx_id(rng, pool) -> int:
    return rng.randrange(4) << 16 | rng.choice([0, 1, 2, 5, 9, rng.randrange(40)])

SPECS += [
    Spec("WCIsvListBase_Destroy", 0x0060b5a0, "thiscall", [], presets=field_or_zero(8, SCRATCH)),
    Spec("WCIsvList_Insert", 0x0060b430, "thiscall", [one_of(WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES)],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("WCIntVector_Construct", 0x004376b0, "thiscall", [small_or_anything(0, 6), anything], new_fails=0.2),
    Spec("WCIntVector_Destroy", 0x00414590, "thiscall", [],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("Lobby_ScaleBy4_44c450", 0x0044c450, "stdcall", [scratch(0), anything, anything, anything, anything]),
    Spec("Win32_CWin32Handle_Set", 0x0060bf00, "thiscall", [anything, anything]),
    Spec("Lobby_MemoryBlock_Assign", 0x00600e80, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(0), new_fails=0.1),
    Spec("Win32_CRegistryKey_Construct", 0x00616430, "thiscall", []),
    Spec("Win32_CEvent_Construct", 0x0060c510, "thiscall", []),
    Spec("Lobby_HasField4_5da3a0", 0x005da3a0, "thiscall", [],
         presets=lambda rng, pool, args: [(4, rng.choice([0, rng.getrandbits(32)]), 4)]),
    Spec("UI_CopyWords_418fb0", 0x00418fb0, "fastcall", [scratch(0), scratch(1), below(20)]),
    Spec("std_wstring_Eos", 0x00428880, "thiscall", [below(30)],
         presets=lambda rng, pool, args: [(4, WORLD_BASE + SCRATCH, 4)]),
    Spec("CGraphicsData_GuiImagePointer", 0x005fac70, "thiscall", [scratch(6), gfx_id], presets=gfx_tables(0x5ac)),
    Spec("CGraphicsData_AnimationPointer", 0x005faaf0, "thiscall", [scratch(6), gfx_id],
         presets=gfx_tables(0x494)),
]


def message_block_source(rng, pool, args):
    """block_world(4) (the message at world 0, another in scratch slot 0)
    plus random fields +0x14..+0x20 of the source."""
    return block_world(4)(rng, pool, args)

SPECS += [
    Spec("WCUIntVector_Destroy", 0x00458590, "thiscall", [],
         presets=lambda rng, pool, args: [(8, rng.choice([0, WORLD_BASE + SCRATCH]), 4)]),
    Spec("WCIntVector_ConstructBare", 0x00437730, "thiscall", [small_or_anything(0, 6)], new_fails=0.2),
    Spec("Lobby_CLMMessageBlock_Copy", 0x00601470, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)],
         presets=message_block_source, new_fails=0.1),
]

SPECS += [
    Spec("GetMemberDword64", 0x0049f510, "thiscall", []),
    Spec("GetMemberByte189", 0x00414030, "thiscall", [], eax_mask=0xFF),
    Spec("GetMemberDword10", 0x0049f4d0, "thiscall", []),
    Spec("GetMemberDword14", 0x0049f590, "thiscall", []),
    Spec("GetMemberDword5C", 0x0049f350, "thiscall", []),
    Spec("GetMemberDword70", 0x0049dec0, "thiscall", []),
    Spec("GetMemberByte68", 0x0049f580, "thiscall", [], eax_mask=0xFF),
    # The three outputs: scratch slots, sometimes the same slot twice.
    Spec("GamePlayerSetup_SavedValues", 0x0049fed0, "thiscall",
         [scratch(0), lambda rng, pool: WORLD_BASE + SCRATCH + rng.choice([0, 0x1000]),
          lambda rng, pool: WORLD_BASE + SCRATCH + rng.choice([0x1000, 0x2000])]),
]


# -- wip/ui-gdi: GameConfig members (game_config.cpp). `this` is the world
# (thiscall), the outputs in CONFIG_OUT; the start map string at +0x15c. --
CONFIG_OUT = 0x1f48000
CONFIG_TEXT = 0x1f48800


def config_mission(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.2 else rng.randint(-3, 52) & 0xFFFFFFFF


def config_start_map(rng, pool, args):
    fields, _ = string_fields(rng, 0x15c, CONFIG_TEXT)
    return fields


def config_kind(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randint(-1, 18) & 0xFFFFFFFF


def config_group61(rng, pool, args):
    return [(0x7a7af0 + 0x159, rng.choice([0, 0, 1, 0x80]), 1)]


SPECS += [
    Spec("GameConfig_LobbyValue68", 0x00413b60, "thiscall", []),
    Spec("GameConfig_SetLobbyValue68", 0x00413b70, "thiscall", [anything]),
    # The two outputs: sometimes the same address.
    Spec("GameConfig_GetLobbyPair148", 0x00413b80, "thiscall",
         [one_of(WORLD_BASE + CONFIG_OUT), one_of(WORLD_BASE + CONFIG_OUT, WORLD_BASE + CONFIG_OUT + 4)]),
    Spec("GameConfig_SetLobbyPair148", 0x00413ba0, "thiscall", [anything, anything]),
    Spec("GameConfig_Byte150", 0x00413bc0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_RandomRaceAmazons", 0x00413bd0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_GfxGroup61", 0x00413be0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_CampaignMissionCD", 0x00413bf0, "thiscall", [config_mission]),
    # The copy goes to a separate string or (rarely) to the config's own.
    # Dead: 0x413c4f (a length above npos), and in the copy-to-itself case
    # (whose fields are cleared first, so the length is 0) the _Xran call
    # 0x413c60 and the erase 0x413c7b / 0x413ca2.
    Spec("GameConfig_StartMap", 0x00413c20, "thiscall",
         [lambda rng, pool: WORLD_BASE + (0x15c if rng.random() < 0.1 else CONFIG_OUT + 4 * rng.randrange(8))],
         presets=config_start_map, globals=null_string_count,
         exempt_blocks=(0x00413c4f, 0x00413c60, 0x00413c7b, 0x00413ca2)),
    Spec("GameConfig_ClearStartMap", 0x00413d50, "thiscall", [], presets=config_start_map),
    Spec("GameConfig_PathIndexForFileKind", 0x00414050, "thiscall", [config_kind], globals=config_group61,
         callees={0x006119b0: (1, "thiscall", "throw")}),  # Win32::ThrowAssertmentFailed
]


def config_serial(rng, pool, args):
    fields, _ = string_fields(rng, 0x04, CONFIG_TEXT)
    return fields


def config_path(rng, pool, args):
    """The path string PathString(out, index) copies (index 5 and above: +0x110)."""
    index = args[1]
    fields, _ = string_fields(rng, 0xc0 + 0x10 * min(index, 5), CONFIG_TEXT)
    return fields


def config_path_out(rng, pool):
    return WORLD_BASE + CONFIG_OUT + 4 * rng.randrange(8)


def config_path_index(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(7)


CONFIG_SERIAL_TEXT = 0x1f48c00
CONFIG_SERIAL_CHARS = b"0123456789" * 4 + b"--- ABCxyz\xe4\xfc\x80\xff"


def config_serial_text(rng, pool, args):
    """A serial-like text at CONFIG_SERIAL_TEXT: digits only, or digits with
    separators, letters and high characters."""
    length = rng.randrange(30)
    digits = rng.random() < 0.35
    text = bytes(rng.choice(b"0123456789" if digits else CONFIG_SERIAL_CHARS) for _ in range(length))
    return [(CONFIG_SERIAL_TEXT + i, c, 1) for i, c in enumerate(text + b"\0")]


SPECS += [
    # Dead as in GameConfig_StartMap (the same inlined copy).
    Spec("GameConfig_SerialNumber", 0x00411a90, "thiscall",
         [lambda rng, pool: WORLD_BASE + (0x04 if rng.random() < 0.1 else CONFIG_OUT + 4 * rng.randrange(8))],
         presets=config_serial, globals=null_string_count,
         exempt_blocks=(0x00411abc, 0x00411acd, 0x00411ae8, 0x00411b0f)),
    Spec("GameConfig_PathString", 0x00412b00, "thiscall", [config_path_out, config_path_index],
         presets=config_path, globals=null_string_count),
    Spec("GameConfig_SerialCheckDigit", 0x00411fd0, "thiscall", [one_of(WORLD_BASE + CONFIG_SERIAL_TEXT)],
         presets=config_serial_text),
]


# -- wip/ui-gdi: GameConfig field accessors (game_config_fields.cpp); `this` is the world. --
SPECS += [
    Spec("GameConfig_Dword28", 0x004120a0, "thiscall", []),
    Spec("GameConfig_Dword78", 0x00412310, "thiscall", []),
    Spec("GameConfig_Dword7C", 0x00412320, "thiscall", []),
    Spec("GameConfig_Dword80", 0x00412330, "thiscall", []),
    Spec("GameConfig_Dword94", 0x00412370, "thiscall", []),
    Spec("GetMemberDword84", 0x00412340, "thiscall", []),
    Spec("GetMemberDword8C", 0x00412350, "thiscall", []),
    Spec("GetMemberDword90", 0x00412360, "thiscall", []),
    Spec("GameConfig_SetDword28", 0x004120b0, "thiscall", [anything]),
    Spec("GameConfig_SetDword2C", 0x004120c0, "thiscall", [anything]),
    Spec("GameConfig_Byte30", 0x004120d0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte38", 0x004120f0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte39", 0x00412100, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte24", 0x00412270, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte58", 0x00412290, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte59", 0x004122a0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5A", 0x004122b0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5B", 0x004122c0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5C", 0x004122e0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5D", 0x004122d0, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5E", 0x00412810, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte5F", 0x00412820, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte6C", 0x00412800, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_Byte15A", 0x00414020, "thiscall", [], eax_mask=0xFF),
    Spec("GameConfig_SetByte24", 0x00412280, "thiscall", [anything], eax_mask=0xFF),
    Spec("GameConfig_SetByte5C", 0x004122f0, "thiscall", [anything], eax_mask=0xFF),
    Spec("GameConfig_SetByte189", 0x00414040, "thiscall", [anything], eax_mask=0xFF),
    Spec("GameConfig_Object3C", 0x00412110, "thiscall", []),
    Spec("GameConfig_String14", 0x00412120, "thiscall", []),
    Spec("GameConfig_HasDirectPlay", 0x004120e0, "thiscall", []),
]


# -- session 55 round 2 (gfx-open): Win32::CFile, Win32::CFileMapping and
# Win32::DetachMemory (win32_mapping.cpp, win32_detach.cpp) -------------
#
# The object is at world 0 (CFile: CWin32Handle layout; CFileMapping: the
# handle, the file at +0xc, the view list at +0x10). The Windows calls are
# API probes (emu.API_HANDLERS: results fixed by the case and the call's
# position, failures included). Win32::ThrowWin32Error 0x610de0 and
# ThrowHResult 0x611050 never return: probed as throwers (the run ends
# there, compared with ECX = the expression, EDX = the file, line, error
# and the message string's 16 bytes, whose buffer is on each frame's own
# stack). CFileNotFound's constructor 0x611db0 is probed (it builds a
# message string: not needed to show the thrower's own logic); the
# _CxxThrowException after it ends the run. The blocks after a thrower
# call are the original's never-reached clean-up of the message string.

MAPPING_FILE = SCRATCH + 8 * SCRATCH_SLOT_BYTES       # a fake CFile (vtable FAKE_VTABLE)
MAPPING_VIEWS = SCRATCH + 9 * SCRATCH_SLOT_BYTES      # the view list's entries
WIN32_TEXT = SCRATCH + 10 * SCRATCH_SLOT_BYTES        # a file name
WIN32_OUT = SCRATCH + 11 * SCRATCH_SLOT_BYTES         # out-parameters (done, high)
DETACH_AREA_LOW = 0x01E00000                          # emu.DETACH_LOW


def thrower_record(machine, ecx, args):
    """ECX, EDX, line, code and the message string's bytes."""
    from unicorn.x86_const import UC_X86_REG_EDX
    message = machine.guest_read(args[-1], 16) if args[-1] else None
    return (ecx, machine.uc.reg_read(UC_X86_REG_EDX), *args[:-1], message)


def file_not_found_record(machine, ecx, args):
    text = machine.guest_read(args[0], 64).split(b"\0")[0] if args[0] else None
    return (text, args[1])


WIN32_THROWERS = {
    0x00610de0: (3, "thiscall", "throw", None, thrower_record),   # ThrowWin32Error(line, error, &message)
    0x00611050: (3, "thiscall", "throw", None, thrower_record),   # ThrowHResult(line, hr, &message)
    0x00611db0: (2, "thiscall", "this", None, file_not_found_record),  # CFileNotFound(name, 1)
}


def win32_text(rng, pool, args):
    """A short file name at WIN32_TEXT."""
    text = rng.choice([b"Gfx\\siedler3_00.f8007e01f.dat", b"x", b"", b"..\\Snd\\Siedler3_00.dat"])
    return [(WIN32_TEXT + i, b, 1) for i, b in enumerate(text + b"\0")]


def name_or_null(rng, pool):
    return 0 if rng.random() < 0.15 else WORLD_BASE + WIN32_TEXT


def out_or_null(rng, pool):
    return 0 if rng.random() < 0.4 else WORLD_BASE + WIN32_OUT


def mapping_world(rng, pool, args):
    """A CFileMapping at world 0: a fake file or none, and a view list with
    entries at MAPPING_VIEWS (length sometimes negative or at capacity,
    growth sometimes 0; vtable the real WCValOrderedVector<void*> one, whose
    find (slot 3) runs)."""
    fields = win32_text(rng, pool, args)
    fields.append((0x0c, 0 if rng.random() < 0.3 else WORLD_BASE + MAPPING_FILE, 4))
    fields.append((MAPPING_FILE, FAKE_VTABLE, 4))
    capacity = rng.choice([0, 1, 2, 5, 10, 10])
    length = rng.choice([0, capacity, capacity, rng.randint(0, max(capacity, 1)), -rng.randint(1, 3)])
    if rng.random() < 0.05:
        capacity = length = 0xFFFFFFFE  # full; capacity + grow wraps: the length is clamped
    fields += [(0x10, 0x65D9D0, 4), (0x18, WORLD_BASE + MAPPING_VIEWS, 4), (0x1c, capacity, 4),
               (0x20, rng.choice([length, rng.getrandbits(32)]) & 0xFFFFFFFF, 4), (0x24, length & 0xFFFFFFFF, 4),
               (0x28, rng.choice([5, 5, 0, 1]), 4)]
    view_pool = [0x10000, 0x20000, 0x30000, 0x40000]
    for i in range(12):
        fields.append((MAPPING_VIEWS + 4 * i, rng.choice(view_pool), 4))
    return fields


def view_arg(rng, pool):
    return rng.choice([0x10000, 0x20000, 0x30000, 0x40000, rng.getrandbits(32)])


def detach_start(rng, pool):
    return WORLD_BASE + DETACH_AREA_LOW + rng.randrange(0x30000)


def detach_size(rng, pool):
    return rng.choice([0, rng.randrange(0x100), rng.randrange(0x8000), rng.randrange(0x30000),
                       rng.getrandbits(32)])


def detach_globals(rng, pool, args):
    return [(0x007E9220, rng.choice([0, 0, 0x1000, 0x800]), 4)]  # g_detachPageSize


# Never reached (exempt below): the clean-up of the message string after
# the thrower call (it never returns), and the frees of the temporaries the
# message is built from, which hold "" and never get a buffer. MapView's
# 0x61a9f7 (the shift loop of insertAt) needs index <= length - 2 with
# index = 0, which only happens with a negative length: dead.
CLEANUP_WIN32_CFILEMAPPING_MAPVIEW = (0x61a911, 0x61a930, 0x61a932, 0x61a936, 0x61a960, 0x61a96c, 0x61a9f7)
CLEANUP_WIN32_CFILEMAPPING_CREATE = (0x61a63b, 0x61a645, 0x61a649, 0x61a64f, 0x61a6a6, 0x61a6bc, 0x61a6c3, 0x61a6c8, 0x61a6f2, 0x61a6fa, 0x61a6fc, 0x61a700, 0x61a726, 0x61a732, 0x61a738, 0x61a744, 0x61a74e, 0x61a752, 0x61a758, 0x61a761)
CLEANUP_WIN32_CFILE_GETSIZE = (0x61a1b5, 0x61a1d4, 0x61a1d6, 0x61a1da, 0x61a204, 0x61a20e, 0x61a318, 0x61a337, 0x61a339, 0x61a33d, 0x61a367, 0x61a371, 0x61a37f)
CLEANUP_WIN32_CFILE_SEEK = (0x619e8f, 0x619eae, 0x619eb0, 0x619eb4, 0x619ede, 0x619ee8, 0x619ff2, 0x61a011, 0x61a013, 0x61a017, 0x61a041, 0x61a04b, 0x61a059)
CLEANUP_WIN32_CFILE_WRITE = (0x619c06, 0x619c10, 0x619c14, 0x619c1a, 0x619c6e, 0x619c85, 0x619c8c, 0x619c91, 0x619cb9, 0x619cc1, 0x619cc3, 0x619cc7, 0x619ced, 0x619cf9, 0x619cfd, 0x619d02, 0x619d0c, 0x619d10, 0x619d28)
CLEANUP_WIN32_CFILE_READ = (0x6199c6, 0x6199d0, 0x6199d4, 0x6199da, 0x619a2e, 0x619a45, 0x619a4c, 0x619a51, 0x619a79, 0x619a81, 0x619a83, 0x619a87, 0x619aad, 0x619ab9, 0x619abd, 0x619ac2, 0x619acc, 0x619ad0, 0x619ae8)
CLEANUP_WIN32_CFILE_OPEN = (0x619774, 0x61977e, 0x619782, 0x619788, 0x6197d8, 0x6197ee, 0x6197f5, 0x6197fa, 0x619822, 0x61982a, 0x61982c, 0x619830, 0x619856, 0x619862, 0x619868, 0x619874, 0x61987e, 0x619882, 0x619888, 0x619891)

SPECS += [
    Spec("Win32_CFile_Open", 0x00619610, "thiscall",
         [name_or_null, anything, anything, anything, anything, anything, anything],
         presets=win32_text, callees=WIN32_THROWERS, new_fails=0.1, new_fails_size=0x28,
         exempt_blocks=CLEANUP_WIN32_CFILE_OPEN),
    Spec("Win32_CFile_Read", 0x006198d0, "thiscall", [anything, anything, out_or_null, anything],
         callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILE_READ),
    Spec("Win32_CFile_Write", 0x00619b10, "thiscall", [anything, anything, out_or_null, anything],
         callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILE_WRITE),
    Spec("Win32_CFile_Seek", 0x00619d50, "thiscall", [anything, out_or_null, anything],
         callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILE_SEEK),
    Spec("Win32_CFile_GetSize", 0x0061a080, "thiscall", [out_or_null], callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILE_GETSIZE),
    Spec("Win32_CFileMapping_Construct", 0x0061a3a0, "thiscall", [], new_fails=0.2),
    Spec("Win32_CFileMapping_Destroy", 0x0061a460, "thiscall", [], presets=mapping_world,
         virtuals={0x00: 1}, exempt_blocks=(0x0061a4de,)),  # data was just set to 0: base destructor's delete
    Spec("Win32_CFileMapping_DeletingDestroy", 0x0061a440, "thiscall", [anything], presets=mapping_world,
         virtuals={0x00: 1}),
    Spec("Win32_CFileMapping_Close", 0x0061a7a0, "thiscall", [], presets=mapping_world, virtuals={0x00: 1}),
    Spec("Win32_CFileMapping_Create", 0x0061a520, "thiscall",
         [one_of(0, 0, WORLD_BASE + MAPPING_FILE), anything, anything, anything, anything, name_or_null],
         presets=mapping_world, callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILEMAPPING_CREATE),
    Spec("Win32_CFileMapping_MapView", 0x0061a7d0, "thiscall", [anything, anything, anything, anything],
         presets=mapping_world, callees=WIN32_THROWERS,
         exempt_blocks=CLEANUP_WIN32_CFILEMAPPING_MAPVIEW),
    Spec("Win32_CFileMapping_UnmapView", 0x0061aa40, "thiscall", [view_arg], presets=mapping_world),
    Spec("Win32_DetachMemory", 0x0061ab60, "fastcall", [detach_start, detach_size], globals=detach_globals,
         callees=WIN32_THROWERS, exempt_blocks=(0x0061b111,)),  # the message's _Tidy after a thrower
]


# -- session 56 (wip/sound): the DirectSound wrappers (directx_sound.cpp) ---
#
# The object under test sits at world 0. COM interfaces point at
# FAKE_VTABLE (COM slots at their byte offsets, "com" virtuals); the game
# objects' own vtables point at FAKE_VTABLE + DS_GAME_SLOTS, so their
# thiscall slots (0x200 + 4 * slot) don't collide with the COM ones.
# g_directSound (0x7e8c0c) points at a fake game object whose +4 is the
# fake IDirectSound. CheckDSResult's exception constructor 0x5c15d0 is
# probed (it builds the message strings; the thrown object is compared as
# the constructor's `this`). GetProcAddress (emu.py) returns the probe of
# slot DS_PROC_SLOT, so Connect's call of DirectSoundCreate is a virtual.

DS_GAME_SLOTS = 0x200
DS_PROC_SLOT = 0x3F0                            # emu.PROC_SLOT
DS_IFACE = SCRATCH + 40 * SCRATCH_SLOT_BYTES   # fake COM interfaces (vtable FAKE_VTABLE), 0x10 apart
DS_GAME = SCRATCH + 41 * SCRATCH_SLOT_BYTES    # the fake g_directSound object
DS_OTHER = SCRATCH + 42 * SCRATCH_SLOT_BYTES   # a second buffer object (copy, assign)
DS_DESC = SCRATCH + 43 * SCRATCH_SLOT_BYTES    # a DSBUFFERDESC
DS_FORMAT = SCRATCH + 44 * SCRATCH_SLOT_BYTES  # a WAVEFORMATEX
DS_GLOBAL = 0x007E8C0C                         # g_directSound

DS_CODES = (0x88780096, 0x88780078, 0x8878000A, 0x80004005, 0x88780064, 0x80070057, 0x8007000E, 1)


def ds_hresult(h):
    """DS_OK half the time, else a DirectSound error (DSERR_BUFFERLOST
    first), a success code or a random value."""
    if h & 1:
        return 0
    choice = (h >> 1) % 10
    if choice < len(DS_CODES):
        return DS_CODES[choice]
    return (h >> 8) & 0xFFFFFFFF


def ds_hr_arg(rng, pool):
    return rng.choice((0, 0, 0) + DS_CODES) if rng.random() < 0.85 else rng.getrandbits(32)


def ds_write_out(index, value, always=False):
    """Virtual effect: writes value(h) to the pointer argument `index`
    (when given), but not always (the callers' uninitialized locals)."""
    def effect(machine, ecx, args, h):
        if args[index] != 0 and (always or h & 3):
            machine.guest_write(args[index], struct.pack("<I", value(h)))
    return effect


def ds_new_buffer(h):
    return WORLD_BASE + DS_IFACE + 0x10 * ((h >> 8) & 3)


def ds_world(null_iface=0.2):
    """The object at world 0: game vtable, interface (or null), flags; the
    other buffer object, g_directSound's object, the fake interfaces and a
    buffer description."""
    def presets(rng, pool, args):
        iface = 0 if rng.random() < null_iface else WORLD_BASE + DS_IFACE
        other = 0 if rng.random() < 0.3 else WORLD_BASE + DS_IFACE
        fields = [(0, FAKE_VTABLE + DS_GAME_SLOTS, 4), (4, iface, 4),
                  (DS_OTHER, rng.getrandbits(32), 4), (DS_OTHER + 4, other, 4),
                  (DS_GAME, FAKE_VTABLE + DS_GAME_SLOTS, 4), (DS_GAME + 4, WORLD_BASE + DS_IFACE, 4),
                  (DS_DESC, 0x14, 4), (DS_DESC + 4, rng.choice([0, rng.getrandbits(32)]), 4),
                  (DS_DESC + 16, rng.choice([0, WORLD_BASE + DS_FORMAT]), 4)]
        fields += [(DS_IFACE + 0x10 * k, FAKE_VTABLE, 4) for k in range(4)]
        return fields
    return presets


def ds_globals(rng, pool, args):
    return [(DS_GLOBAL, WORLD_BASE + DS_GAME, 4)]


DS_THROWERS = {0x005c15d0: (3, "thiscall", "this")}  # CDirectSoundException(hr, where, 1)
DS_RELEASE = {0x08: (1, {}, {"com": True})}
DS_ADDREF = {0x04: (1, {}, {"com": True})}
DS_GAME_VIRTUALS = {DS_GAME_SLOTS + 0x04: 0, DS_GAME_SLOTS + 0x08: 0, DS_GAME_SLOTS + 0x0c: 0}


def ds_com(offset, count, pointers=None):
    """A COM method returning an HRESULT, plus the game objects' slots."""
    v = dict(DS_GAME_VIRTUALS)
    v[offset] = (count, pointers or {}, {"com": True, "returns": ds_hresult})
    return v


SPECS += [
    Spec("DirectX_DSErrorText", 0x005c1c10, "fastcall", [ds_hr_arg]),
    Spec("DirectX_CheckDSResult", 0x005c1c40, "thiscall", [ds_hr_arg, anything], callees=DS_THROWERS,
         new_fails=0.1, new_fails_size=0x20),
    Spec("DirectX_CDirectSound_Construct", 0x005c1cc0, "thiscall", []),
    Spec("DirectX_CDirectSound_Destroy", 0x005c1d00, "thiscall", [], presets=ds_world(0.3),
         virtuals=DS_RELEASE),
    Spec("DirectX_CDirectSound_DeletingDestroy", 0x005c1ce0, "thiscall", [anything], presets=ds_world(0.3),
         virtuals=DS_RELEASE),
    Spec("DirectX_CDirectSound_Connect", 0x005c1d20, "thiscall", [one_of(0, WORLD_BASE + DS_FORMAT)],
         presets=ds_world(), callees=DS_THROWERS, new_fails=0.05, new_fails_size=0x20,
         virtuals={DS_PROC_SLOT: (3, {1: 4}, {"com": True, "returns": ds_hresult})},
         virtual_effects={DS_PROC_SLOT: ds_write_out(1, lambda h: WORLD_BASE + DS_IFACE)}),
    Spec("DirectX_CDirectSound_Disconnect", 0x005c1da0, "thiscall", [], presets=ds_world(0),
         virtuals={**DS_GAME_VIRTUALS, **DS_RELEASE}),
    Spec("DirectX_CDirectSound_DSDuplicateSoundBuffer", 0x005c1f40, "thiscall", [anything, anything],
         presets=ds_world(0), callees=DS_THROWERS, virtuals=ds_com(0x14, 3)),
    Spec("DirectX_CDirectSoundBuffer_Release", 0x005c2030, "thiscall", [], presets=ds_world(0),
         virtuals=DS_RELEASE),
    Spec("DirectX_CDirectSoundBuffer_Destroy", 0x005c1fe0, "thiscall", [], presets=ds_world(0.3),
         virtuals=DS_RELEASE),
    Spec("DirectX_CDirectSoundBuffer_DeletingDestroy", 0x005c1f90, "thiscall", [anything], presets=ds_world(0.3),
         virtuals=DS_RELEASE),
    Spec("DirectX_CPrimaryBuffer_DeletingDestroy", 0x005c21a0, "thiscall", [anything], presets=ds_world(0.3),
         virtuals=DS_RELEASE),
    Spec("DirectX_CDirectSoundBuffer_CopyConstruct", 0x005c1fb0, "thiscall", [one_of(WORLD_BASE + DS_OTHER)],
         presets=ds_world(), virtuals=DS_ADDREF),
    Spec("DirectX_CDirectSoundBuffer_Assign", 0x005c2120, "thiscall",
         [one_of(WORLD_BASE + DS_OTHER, WORLD_BASE + DS_OTHER, WORLD_BASE)], presets=ds_world(), virtuals=DS_ADDREF),
    Spec("DirectX_CDirectSoundBuffer_CheckResult", 0x005c2000, "thiscall", [ds_hr_arg, anything],
         presets=ds_world(), callees=DS_THROWERS, virtuals=DS_GAME_VIRTUALS),
    Spec("DirectX_CDirectSoundBuffer_SetCreateFlags", 0x005c2050, "thiscall", [anything]),
    Spec("DirectX_CDirectSoundBuffer_DSCreateSoundBuffer", 0x005c2060, "thiscall", [one_of(WORLD_BASE + DS_DESC)],
         presets=ds_world(), globals=ds_globals, callees=DS_THROWERS, virtuals=ds_com(0x0c, 4, {2: 4}),
         virtual_effects={0x0c: ds_write_out(2, ds_new_buffer)}),
    Spec("DirectX_CDirectSoundBuffer_DSGetStatus", 0x005c20b0, "thiscall", [], presets=ds_world(0),
         callees=DS_THROWERS, virtuals=ds_com(0x24, 2, {1: 4}),
         virtual_effects={0x24: ds_write_out(1, lambda h: (h >> 8) & 0xF)}),
    Spec("DirectX_CDirectSoundBuffer_RestoreAll", 0x005c20f0, "thiscall", [], presets=ds_world(),
         globals=ds_globals, virtuals=DS_GAME_VIRTUALS,
         exempt_blocks=(0x5c20fb,)),  # the nop padding after its tail jump
    Spec("DirectX_CDirectSoundBuffer_DSRestore", 0x005c2100, "thiscall", [], presets=ds_world(0),
         callees=DS_THROWERS, virtuals=ds_com(0x50, 1)),
    Spec("DirectX_CDirectSoundBuffer_DSGetVolume", 0x005c2150, "thiscall", [anything], presets=ds_world(0),
         callees=DS_THROWERS, virtuals=ds_com(0x18, 2)),
    Spec("DirectX_CPrimaryBuffer_Construct", 0x005c2180, "thiscall", []),
    # Create's out local starts as the address of its own stack temporary
    # (DSCreateSoundBuffer's argument slot): different in any two
    # implementations, so it is not recorded and the fake always writes it,
    # as DirectSound does on success (the standalone DSCreateSoundBuffer
    # spec covers the not-written case).
    Spec("DirectX_CPrimaryBuffer_Create", 0x005c21c0, "thiscall", [], presets=ds_world(),
         globals=ds_globals, callees=DS_THROWERS, virtuals=ds_com(0x0c, 4, {1: 0x14, 2: ()}),
         virtual_effects={0x0c: ds_write_out(2, ds_new_buffer, always=True)}),
    Spec("DirectX_CSecondaryBuffer_Construct", 0x005c2200, "thiscall", []),
    Spec("DirectX_CSecondaryBuffer_Create", 0x005c2220, "thiscall", [one_of(0, WORLD_BASE + DS_FORMAT), anything],
         presets=ds_world(), globals=ds_globals, callees=DS_THROWERS,
         virtuals=ds_com(0x0c, 4, {1: 0x14, 2: ()}),
         virtual_effects={0x0c: ds_write_out(2, ds_new_buffer, always=True)}),
    Spec("DirectX_CSoundEffectBuffer_Construct", 0x005c28b0, "thiscall", [anything]),
]


# App::WaitTime (wait_times.cpp, session 57): kinds 0..8, the -debug
# switch (0x7e916c) zero or not.
SPECS += [
    Spec("App_WaitTime", 0x0040d0e0, "fastcall", [below(9), anything],
         globals=lambda rng, pool, args: [(0x7e916c, rng.choice([0, 0, 1, 0x100]), 4)]),
]


# -- session 57 (wip/sound-fx): the sound files and the DirectSound exception
# (directx_sound_files.cpp) ---------------------------------------------------
#
# Objects at world offset 0. CSoundFile's name is a copy of the string in
# scratch slot 0 (string_world; sometimes its own member at +4, the
# self-assignment path). CMemorySoundFile gets a WAVEFORMATEX in SFX_FORMAT;
# Read reads from SFX_DATA into SFX_OUT.

SFX_FORMAT = SCRATCH + 80 * SCRATCH_SLOT_BYTES
SFX_DATA = SCRATCH + 81 * SCRATCH_SLOT_BYTES
SFX_OUT = SCRATCH + 82 * SCRATCH_SLOT_BYTES


def sfx_name_ptr(rng, pool) -> int:
    return WORLD_BASE + 4 if rng.random() < 0.1 else STRING_OTHER_PTR


def sfx_format(rng, pool, args):
    """A WAVEFORMATEX: mostly 8- or 16-bit, rates around 22,050 Hz."""
    fields = [(SFX_FORMAT + k, rng.getrandbits(8), 1) for k in range(0x12)]
    fields.append((SFX_FORMAT + 4, rng.choice([22050, 22049, 22051, 44100, 11025, rng.getrandbits(32)]), 4))
    fields.append((SFX_FORMAT + 0xE, rng.choice([16, 16, 8, 24, rng.getrandbits(16)]), 2))
    return fields


def sfx_mode(rng, pool) -> int:
    return rng.choice([1, 2, 4, 4, 2, 0, 3, rng.getrandbits(32)])


def sfx_read_file(rng, pool, args):
    """A CMemorySoundFile reading SFX_DATA from a small position."""
    return [(0x28, WORLD_BASE + SFX_DATA, 4), (0x30, rng.randrange(0x40), 4),
            (0x34, rng.choice([1, 2, 4, 1, 2, 4, 0, 3, rng.getrandbits(32)]), 4)]


def sfx_exception(rng, pool, args):
    """A CDirectSoundException: its code and the call's name (or null)."""
    fields = string_world(rng, pool, args)
    fields.append((0x18, rng.choice(DS_CODES + (0, 0, rng.getrandbits(32))), 4))
    fields.append((0x1C, rng.choice([0, STRING_TEXT_PTR, STRING_TEXT_PTR]), 4))
    return fields


SPECS += [
    # Dead: 0x5c229e caps a length above 0xFFFFFFFF; 0x5c22af, 0x5c22ca and
    # 0x5c22f2 are the _Xran, erase-memmove and _Eos of the inlined
    # self-assignment, whose length the constructor has just set to 0 (as in
    # Win32_CException_Construct); 0x5c237c reads the static empty string
    # for a source without buffer but with a length, which string_fields
    # makes only as an over-long string (an invalid case: it faults).
    Spec("DirectX_CSoundFile_Construct", 0x005c2270, "thiscall", [sfx_name_ptr], presets=string_world,
         exempt_blocks=(0x005c229e, 0x005c22af, 0x005c22ca, 0x005c22f2, 0x005c237c)),
    # Dead: 0x5c2492 is the _Eos after erase(0, npos) (the new length is 0,
    # for which _Grow returns false); 0x5c24b3 decrements the count of a
    # buffer that the split before has just made unshared.
    Spec("DirectX_CSoundFile_Destroy", 0x005c23d0, "thiscall", [], eax_mask=0, presets=member_strings((0x04,)),
         exempt_blocks=(0x005c2492, 0x005c24b3)),
    # ignore_bytes: the name's allocator byte (+4) is the low byte of the
    # address of the constructor's own temporary (CSoundFile's copy takes it
    # from its argument's slot); never read. Dead: 0x5c25c6 deletes the
    # first temporary's buffer, which the name always shares by then (count
    # >= 1); 0x5c266b is the _Eos after erase(0, npos) (see above); 0x5c2689
    # / 0x5c268d test and decrement the count of the name's buffer after the
    # split made it unshared.
    Spec("DirectX_CMemorySoundFile_Construct", 0x005c24e0, "thiscall",
         [one_of(WORLD_BASE + SFX_FORMAT), anything, anything, sfx_mode], presets=sfx_format,
         ignore_bytes=(0x04,), exempt_blocks=(0x005c25c6, 0x005c266b, 0x005c2689, 0x005c268d)),
    Spec("DirectX_CMemorySoundFile_Rewind", 0x005c27b0, "thiscall", [], eax_mask=0),
    Spec("DirectX_CMemorySoundFile_GetFormat", 0x005c27c0, "thiscall",
         [one_of(WORLD_BASE + SFX_OUT), one_of(WORLD_BASE + SFX_OUT + 0x20)]),
    Spec("DirectX_CMemorySoundFile_Read", 0x005c2800, "thiscall",
         [lambda rng, pool: rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(0x100)]), one_of(WORLD_BASE + SFX_OUT)],
         presets=sfx_read_file),
    # Dead: 0x5c16f8 / 0x5c1722 erase the message "" (length 0: no memmove,
    # _Grow(0) false); 0x5c1776 / 0x5c177a split the description, whose
    # buffer is unshared (Describe frees the temporary it was copied from);
    # 0x5c17d7 is the _Eos after its erase(0, npos).
    Spec("DirectX_CDirectSoundException_Construct", 0x005c15d0, "thiscall",
         [lambda rng, pool: rng.choice(DS_CODES + (0, rng.getrandbits(32))), text_or_null, mostly_derived],
         presets=string_world, exempt_blocks=(0x005c16f8, 0x005c1722, 0x005c1776, 0x005c177a, 0x005c17d7)),
    # ignore_bytes: the result's allocator byte comes from an uninitialized
    # local of the original (never read), as in Win32_CAssertmentFailed_Describe.
    Spec("DirectX_CDirectSoundException_Describe", 0x005c1930, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=sfx_exception, ignore_bytes=(STRING_RESULT,)),
    # Dead (both destructors): the _Eos after erase(0, npos) (0x5c18cd,
    # 0x5b897d) and the count decrement after the split (0x5c18ef, 0x5b899f).
    Spec("DirectX_CDirectSoundException_Destroy", 0x005c1830, "thiscall", [], eax_mask=0,
         presets=member_strings((0x08,)), exempt_blocks=(0x005c18cd, 0x005c18ef)),
    Spec("DirectX_CAppNotActiveException_Destroy", 0x005b88e0, "thiscall", [], eax_mask=0,
         presets=member_strings((0x08,)), exempt_blocks=(0x005b897d, 0x005b899f)),
    Spec("DirectX_CAppNotActiveException_Construct", 0x005b8890, "thiscall", [message_ptr, mostly_derived],
         presets=string_world),
    Spec("Win32_CMCIException_Destroy", 0x00614150, "thiscall", [], eax_mask=0,
         presets=member_strings((0x08,)), exempt_blocks=(0x006141ed, 0x0061420f)),  # as ~CAppNotActiveException
]


# -- session 57: DirectX::CSoundEffect and its dictionary of duplicates
# (directx_sound_effect.cpp) --------------------------------------------------
#
# The effect sits at world 0 (0xc0 bytes): its dictionary (+4) is a level-0
# chain of SFX_NODES (0x20 apart: key, a CSoundEffectBuffer, next; a node
# pointer points at its next field), the master (+0x90) and the duplicates
# have the real CSoundEffectBuffer vtable (their Release / DSRestore /
# GetStatus run) and fake interfaces (DS_IFACE); the file (+0xa0) is a fake
# game object (FAKE_VTABLE + DS_GAME_SLOTS: delete, Open, Close, GetFormat,
# Read). The Watcom library's find / insert / remove / clear and the base
# constructor are probed (as in sound.py), so are the functions with specs
# of their own where these call them (Unload, Restore, the lost-buffer
# restore 0x5c28e0, PurgeStoppedDuplicates, DuplicateInto) and the
# exception constructors. Lock writes its four outputs (a lock area in
# SFX_LOCK).

SFX_NODES = SCRATCH + 83 * SCRATCH_SLOT_BYTES
SFX_FILE = SCRATCH + 84 * SCRATCH_SLOT_BYTES
SFX_LOCK = SCRATCH + 85 * SCRATCH_SLOT_BYTES
SFX_APP = SCRATCH + 86 * SCRATCH_SLOT_BYTES
SFX_PAIR = SCRATCH + 87 * SCRATCH_SLOT_BYTES
SFX_BLOCK = SCRATCH + 88 * SCRATCH_SLOT_BYTES
SFX_ALLOC_FN = 0x00401000   # code addresses the node allocator presets point at (probed)
SFX_FREE_FN = 0x00401004
SFX_EFFECT_VTABLE = 0x0065cf4c
SFX_BUFFER_VTABLE = 0x0065cf38
SFX_NODE_COUNT = 4


def sfx_node(k):
    return WORLD_BASE + SFX_NODES + 0x20 * k + 0x14


def sfx_buffer(fields, offset, rng, effect=WORLD_BASE):
    """A CSoundEffectBuffer at world `offset` with a fake interface."""
    iface = WORLD_BASE + DS_IFACE + 0x10 * rng.randrange(4)
    fields += [(offset, SFX_BUFFER_VTABLE, 4), (offset + 4, iface, 4),
               (offset + 8, rng.choice([0, 8, rng.getrandbits(32)]), 4),
               (offset + 0xC, rng.choice([effect, effect, 0, rng.getrandbits(32)]), 4)]
    return fields


def sfx_common(fields, rng):
    fields += [(DS_IFACE + 0x10 * k, FAKE_VTABLE, 4) for k in range(4)]
    fields += [(DS_GAME, FAKE_VTABLE + DS_GAME_SLOTS, 4), (DS_GAME + 4, WORLD_BASE + DS_IFACE, 4),
               (SFX_FILE, FAKE_VTABLE + DS_GAME_SLOTS, 4), (SFX_APP + 0x1C, rng.choice([1, 1, 0, 2]), 1)]
    return fields


def sfx_dict(fields, offset, rng, count=None):
    """The duplicates' dictionary at world `offset`: header, level-0 chain."""
    n = rng.choice([0, 1, 2, 3, 4]) if count is None else count
    header = offset + 8
    fields += [(offset + 0x70, WORLD_BASE + header, 4), (offset + 0x74, n, 4), (offset + 0x6C, 0, 4),
               (offset + 0x64, 8, 4), (offset + 0x5C, 0, 4), (offset + 0x60, 0, 4)]
    fields += [(header + 4 * k, 0, 4) for k in range(20)]
    previous = header
    keys = sorted(rng.sample(range(-3, 40), SFX_NODE_COUNT))
    for k in range(SFX_NODE_COUNT):
        node = SFX_NODES + 0x20 * k
        fields.append((node, keys[k] & 0xFFFFFFFF, 4))
        sfx_buffer(fields, node + 4, rng)
        fields.append((node + 0x14, 0, 4))
        if k < n:
            fields.append((previous, sfx_node(k), 4))
            previous = node + 0x14
    sfx_buffer(fields, offset + 0x7C, rng)
    return fields


def sfx_effect_world(rng, pool, args):
    fields = [(0, SFX_EFFECT_VTABLE, 4), (0xA0, rng.choice([WORLD_BASE + SFX_FILE] * 4 + [0]), 4),
              (0xB8, rng.choice([0, 1, 2, 4, rng.randrange(0x80)]), 4), (0xBC, rng.getrandbits(32), 4)]
    fields = sfx_dict(fields, 4, rng)
    sfx_buffer(fields, 0x90, rng)
    return sfx_common(fields, rng)


def sfx_globals(rng, pool, args):
    return [(DS_GLOBAL, WORLD_BASE + DS_GAME, 4), (0x007E9168, WORLD_BASE + SFX_APP, 4),
            (0x007A0C00, rng.choice([0, 7, rng.getrandbits(32)]), 4)]


def sfx_lookup(missing):
    """find / insert: one of the nodes, or none (find often, insert rarely)."""
    def returns(h):
        if (missing and h % 3 == 0) or (not missing and h % 17 == 0):
            return 0
        return sfx_node((h >> 8) % SFX_NODE_COUNT)
    return returns


def sfx_lock(machine, ecx, args, h):
    """Lock's outputs: the first part in SFX_LOCK, sometimes a second one."""
    size = args[2] & 0xFFFF
    first = size if h & 1 else size // 2
    second = size - first
    for index, value in ((3, WORLD_BASE + SFX_LOCK), (4, first),
                         (5, WORLD_BASE + SFX_LOCK + 0x800 if second else 0), (6, second)):
        if args[index]:
            machine.guest_write(args[index], struct.pack("<I", value & 0xFFFFFFFF))


def sfx_get_format(machine, ecx, args, h):
    """The file's GetFormat: a format and a small size."""
    if args[0]:
        machine.guest_write(args[0], bytes(((h >> (k % 56)) & 0xFF) for k in range(0x12)))
    if args[1]:
        machine.guest_write(args[1], struct.pack("<I", (h >> 20) & 0x7F))


def sfx_key_record(machine, ecx, args):
    """A find / insert / remove call: the dictionary and the key's value."""
    key = struct.unpack("<I", machine.guest_read(args[0], 4))[0] if args[0] else None
    return (ecx, key)


SFX_DICT_PROBES = {
    0x0060bab0: (1, "thiscall", sfx_lookup(True), None, sfx_key_record),   # base_find
    0x0060bbe0: (1, "thiscall", sfx_lookup(False), None, sfx_key_record),  # base_insert
    0x0060be20: (1, "thiscall", "probe", None, sfx_key_record),            # base_remove
    0x0060bd80: (0, "thiscall", "probe"),                                  # clear
}
SFX_CALLEES = {
    **DS_THROWERS, **SFX_DICT_PROBES,
    0x00610960: (1, "thiscall", "throw"),   # ThrowOutOfMemory(text, file, line)
    # CSoundEffectBuffer::Restore (a lost buffer; own spec), recorded
    # without ECX: on the duplicates it is a stack copy's address.
    0x005c28e0: (0, "stdcall", "probe"),
}


def sfx_com(*entries):
    """COM methods (offset, argument count, pointers) returning HRESULTs,
    plus the file's slots and AddRef / Release."""
    v = {DS_GAME_SLOTS + 0x00: 1, DS_GAME_SLOTS + 0x04: 0, DS_GAME_SLOTS + 0x08: 0,
         DS_GAME_SLOTS + 0x0c: (2, {}), DS_GAME_SLOTS + 0x10: 2,
         0x04: (1, {}, {"com": True}), 0x08: (1, {}, {"com": True})}
    for offset, count, pointers in entries:
        v[offset] = (count, pointers, {"com": True, "returns": ds_hresult})
    return v


SFX_LOCK_VIRTUAL = (0x2C, 8, {3: (), 4: (), 5: (), 6: ()})
SFX_UNLOCK_VIRTUAL = (0x4C, 5, {})
# GetStatus' out local starts as the stack slot of DSGetStatus' ECX, on the
# duplicates a stack copy's address: not recorded.
SFX_STATUS_VIRTUAL = (0x24, 2, {1: ()})
SFX_EFFECTS = {0x2C: sfx_lock, DS_GAME_SLOTS + 0x0c: sfx_get_format,
               0x24: ds_write_out(1, lambda h: (h >> 8) & 0xF),
               0x0C: ds_write_out(2, ds_new_buffer, always=True)}


def sfx_dict_world(rng, pool, args):
    fields = sfx_dict([], 0, rng)
    fields += [(0x5C, rng.choice([0, 0, SFX_ALLOC_FN]), 4), (0x60, rng.choice([0, 0, SFX_FREE_FN]), 4)]
    fields.append((SFX_PAIR, rng.choice([0, 5, rng.getrandbits(32)]), 4))
    sfx_buffer(fields, SFX_PAIR + 4, rng)
    return sfx_common(fields, rng)


def sfx_iter_world(rng, pool, args):
    fields = sfx_dict([], 0x40, rng, count=SFX_NODE_COUNT)
    fields += [(0, 0x0065cf8c, 4), (8, WORLD_BASE + 0x40, 4),
               (0x0C, rng.choice([0, sfx_node(0), sfx_node(1), sfx_node(3)]), 4), (0x10, 0, 4)]
    return sfx_common(fields, rng)


def sfx_buffer_world(rng, pool, args):
    fields = sfx_buffer([], 0, rng, effect=WORLD_BASE + 0x100)
    sfx_buffer(fields, SFX_PAIR, rng)
    return sfx_common(fields, rng)


def sfx_sound_world(rng, pool, args):
    return sfx_common([(0, FAKE_VTABLE + DS_GAME_SLOTS, 4)], rng)


SPECS += [
    Spec("DirectX_CSoundEffect_Construct", 0x005c2aa0, "thiscall", [],
         callees={0x0060b9f0: (3, "thiscall", "this")}),    # WCSkipNonTempBase (Watcom library)
    Spec("DirectX_CSoundEffect_Destroy", 0x005c2b00, "thiscall", [], eax_mask=0, presets=sfx_effect_world,
         callees={**SFX_CALLEES, 0x005c2ce0: (0, "thiscall", "probe")}, virtuals=sfx_com()),
    Spec("DirectX_CSoundEffect_Unload", 0x005c2ce0, "thiscall", [], presets=sfx_effect_world,
         callees=SFX_CALLEES, virtuals=sfx_com()),
    Spec("DirectX_CSoundEffect_Load", 0x005c2ba0, "thiscall", [one_of(WORLD_BASE + SFX_FILE), anything],
         eax_mask=0, presets=sfx_effect_world, globals=sfx_globals, callees=SFX_CALLEES, new_fails=0.05,
         virtuals=sfx_com(SFX_LOCK_VIRTUAL, SFX_UNLOCK_VIRTUAL, (0x0C, 4, {1: 0x14, 2: ()})),
         virtual_effects=SFX_EFFECTS),
    Spec("DirectX_CSoundEffect_Restore", 0x005c2d20, "thiscall", [], eax_mask=0, presets=sfx_effect_world,
         globals=sfx_globals, callees=SFX_CALLEES, new_fails=0.05,
         virtuals=sfx_com(SFX_LOCK_VIRTUAL, SFX_UNLOCK_VIRTUAL, (0x50, 1, {})), virtual_effects=SFX_EFFECTS),
    Spec("DirectX_CSoundEffect_Play", 0x005c2fc0, "thiscall",
         [lambda rng, pool: -rng.randrange(10001) & 0xFFFFFFFF,
          lambda rng, pool: rng.randint(-10000, 10000) & 0xFFFFFFFF, below(2)],
         presets=sfx_effect_world, globals=sfx_globals,
         callees={**SFX_CALLEES, 0x005c3210: (0, "thiscall", "probe"), 0x005c2a60: (1, "thiscall", "probe")},
         virtuals=sfx_com(SFX_STATUS_VIRTUAL, (0x30, 4, {}), (0x3C, 2, {}), (0x40, 2, {})),
         virtual_effects=SFX_EFFECTS,
         # The iterator's "no current node" branch right after a successful
         # operator++ (which leaves a node): dead (as in PurgeEffects).
         exempt_blocks=(0x005c30b3,)),
    Spec("DirectX_CSoundEffect_PurgeStoppedDuplicates", 0x005c3210, "thiscall", [], eax_mask=0xFF,
         presets=sfx_effect_world, globals=sfx_globals, callees=SFX_CALLEES,
         virtuals=sfx_com(SFX_STATUS_VIRTUAL), virtual_effects=SFX_EFFECTS,
         exempt_blocks=(0x005c32d4,)),   # the same dead branch as in Play
    Spec("DirectX_CSoundEffectBuffer_Restore", 0x005c28e0, "thiscall", [], presets=sfx_buffer_world,
         globals=sfx_globals, callees={0x005c2d20: (0, "thiscall", "probe")}, new_fails=0.1, new_fails_size=0x18),
    Spec("DirectX_CDirectSound_RestoreIfActive", 0x005c1dc0, "thiscall", [], presets=sfx_sound_world,
         globals=sfx_globals, virtuals={DS_GAME_SLOTS + 0x0c: 0}, new_fails=0.1, new_fails_size=0x18),
    Spec("DirectX_CSoundEffectBuffer_DuplicateInto", 0x005c2a60, "thiscall", [one_of(WORLD_BASE + SFX_PAIR)],
         presets=sfx_buffer_world, globals=sfx_globals, callees=DS_THROWERS,
         virtuals=sfx_com((0x14, 3, {}))),
    Spec("EffectBufferDict_NewNode", 0x005c3510, "thiscall", [one_of(WORLD_BASE + SFX_PAIR), below(8)],
         presets=sfx_dict_world, new_fails=0.1, virtuals=sfx_com(),
         callees={SFX_ALLOC_FN: (0, "thiscall", lambda h: 0 if h % 5 == 0 else WORLD_BASE + SFX_BLOCK)}),
    Spec("EffectBufferDict_DestroyNode", 0x005c35c0, "thiscall",
         [lambda rng, pool: rng.choice([0, sfx_node(0), sfx_node(2)]), below(8)],
         presets=sfx_dict_world, virtuals=sfx_com(), callees={SFX_FREE_FN: (0, "thiscall", "probe")}),
    Spec("EffectBufferDict_Index", 0x005c3600, "thiscall", [one_of(WORLD_BASE + SFX_PAIR)],
         presets=sfx_dict_world, callees=SFX_DICT_PROBES),
    # cover_until: Ghidra's function also takes in the padding and the
    # static initializer that follow it (0x5c3700).
    Spec("EffectBufferDictIter_Value", 0x005c3640, "thiscall", [one_of(WORLD_BASE + SFX_BLOCK)],
         presets=sfx_iter_world, virtuals=sfx_com(), cover_until=0x005c36f0),
]


# -- session 57: Win32::CCDAudio (win32_cdaudio.cpp) ----------------------------
# The object at world 0 (device +8, window +0xc, often 0). The import slots
# of mciSendCommandA and PostMessageA point at probe code (as GetProcAddress'
# PROC_SLOT), so the calls are recorded like COM calls: the MCI parameter
# block (on the stack) by the fields the original sets.
CD_MCI_SLOT = 0x3D0
CD_POST_SLOT = 0x3D4
CD_VIRTUAL_CODE = FAKE_VTABLE + 0x1000   # emu.VIRTUAL_CODE


def cd_world(rng, pool, args):
    return [(8, rng.choice([0, 1, rng.getrandbits(32)]), 4), (0xC, rng.choice([0, rng.getrandbits(32)]), 4)]


def cd_globals(rng, pool, args):
    return [(0x0065536C, CD_VIRTUAL_CODE + CD_MCI_SLOT, 4), (0x006552F0, CD_VIRTUAL_CODE + CD_POST_SLOT, 4)]


def cd_mci_result(h):
    return (0, 0, 0x106, 0x101, (h >> 8) & 0xFFFFFFFF)[h % 5]


def cd_status_return(machine, ecx, args, h):
    if args[3]:
        machine.guest_write(args[3] + 4, struct.pack("<I", (h >> 12) & 0x7F))


def cd_spec(name, address, args, parameter_fields):
    return Spec(name, address, "thiscall", args, presets=cd_world, globals=cd_globals,
                virtuals={CD_MCI_SLOT: (4, {3: parameter_fields}, {"com": True, "returns": cd_mci_result}),
                          CD_POST_SLOT: (4, {}, {"com": True})},
                virtual_effects={CD_MCI_SLOT: cd_status_return} if address == 0x00615fc0 else None)


SPECS += [
    Spec("Win32_CCDAudio_Construct", 0x006154c0, "thiscall", []),
    cd_spec("Win32_CCDAudio_SetTimeFormatTMSF", 0x00615e60, [], ((4, 4),)),
    cd_spec("Win32_CCDAudio_Play", 0x00615ea0, [anything, anything], ()),
    cd_spec("Win32_CCDAudio_Stop", 0x00615f70, [], ()),
    cd_spec("Win32_CCDAudio_DoStop", 0x00615f90, [], ()),
    cd_spec("Win32_CCDAudio_GetTrackCount", 0x00615fc0, [], ((8, 4),)),
]


# -- wip/ddraw-render: the DirectDraw wrappers (directx_draw.cpp) ------------
# Called "fastcall": args[0] = `this` (ECX), args[1] = EDX (ignored by both),
# so the virtual-base parts can point into the object. The object sits at
# DDW_OBJ (room below for a flip surface's parts), a second CDirectDraw or
# destination surface at DDW_OTHER, the argument buffers after it. COM
# objects (vtable FAKE_VTABLE): the IDirectDraw2 at DDW_COM, surfaces
# 0x10 apart after it, a palette at +0x100. The owner (a CDirectDraw: game
# vtable FAKE_VTABLE + DS_GAME_SLOTS, +4 the IDirectDraw2) at DDW_OWNER.
# Imports go to probes through their import slots (globals): SetRect,
# SendMessageA, CoCreateInstance; DirectDrawEnumerateA is GetProcAddress'
# PROC_SLOT. The surface descriptions the Creates build on the stack are
# recorded by the fields they set; CreateSurface's effect copies a named
# pixel format to DDW_PF_SEEN, so it is compared as memory.
DDW_OBJ = 0x100
DDW_OTHER = 0x200
DDW_DESC = 0x280
DDW_RECT = 0x300
DDW_CAPS_A = 0x400
DDW_CAPS_B = 0x580
DDW_PF = 0x700
DDW_OUT = 0x740
DDW_COM = SCRATCH + 60 * SCRATCH_SLOT_BYTES
DDW_OWNER = SCRATCH + 61 * SCRATCH_SLOT_BYTES
DDW_GAMEOBJ = SCRATCH + 62 * SCRATCH_SLOT_BYTES
DDW_PF_SEEN = SCRATCH + 63 * SCRATCH_SLOT_BYTES
DDW_APP = SCRATCH + 64 * SCRATCH_SLOT_BYTES
DDW_SETRECT_SLOT = 0x3C0
DDW_SENDMESSAGE_SLOT = 0x3C4
DDW_COCREATE_SLOT = 0x3C8
DDW_VIRTUAL_CODE = FAKE_VTABLE + 0x1000   # emu.VIRTUAL_CODE
DDW_GAME_VTABLE = FAKE_VTABLE + DS_GAME_SLOTS
DDW_CODES = (0x887601C2, 0x8876021C, 0x8876023F, 0x8876024B, 0x887600E1, 0x80004002, 0x80004005, 1)


def ddw_hresult(h):
    """DD_OK most often, else one of the codes the wrappers test (lost,
    still drawing, no blit hardware, wrong mode, no exclusive mode, no
    interface), E_FAIL or a success code."""
    choice = h % 16
    return 0 if choice < 8 else DDW_CODES[choice - 8]


def ddw_surface(h):
    return WORLD_BASE + DDW_COM + 0x10 * (1 + ((h >> 12) & 7))


def ddw_write(index, value):
    """Effect: *args[index] = value(h), when the pointer is given."""
    def effect(machine, ecx, args, h):
        if args[index]:
            machine.guest_write(args[index], struct.pack("<I", value(h) & 0xFFFFFFFF))
    return effect


def ddw_create_surface(machine, ecx, args, h):
    """CreateSurface(desc, out, outer): *out = a fake surface; a pixel
    format the flags name is copied to DDW_PF_SEEN."""
    flags = struct.unpack("<I", machine.guest_read(args[1] + 4, 4))[0]
    if flags & 0x1000:
        machine.guest_write(WORLD_BASE + DDW_PF_SEEN, machine.guest_read(args[1] + 0x48, 32))
    if args[2]:
        machine.guest_write(args[2], struct.pack("<I", ddw_surface(h)))


def ddw_fill_desc(index):
    """Effect: a surface description with random height, width, pitch,
    refresh rate and bit count."""
    def effect(machine, ecx, args, h):
        desc = args[index]
        if desc:
            for offset, value in ((8, (480, 600, 768, h & 0x7FF)[(h >> 2) & 3]),
                                  (0xC, (640, 800, 1024, (h >> 11) & 0x7FF)[(h >> 22) & 3]),
                                  (0x10, (h >> 24) & 0xFFF), (0x18, (0, 60, 75, (h >> 36) & 0x7F)[(h >> 34) & 3]),
                                  (0x54, (16, 16, 8, (h >> 44) & 0x3F)[(h >> 42) & 3])):
                machine.guest_write(desc + offset, struct.pack("<I", value))
    return effect


def ddw_com(*entries):
    """COM methods (offset, stack arguments incl. the interface, pointers)
    returning ddw_hresult, plus AddRef / Release, the owner's and the
    CDirectDraw's game slots (3 restore, 4 restore all, 5 and 6 release
    surfaces / palettes (bool), 0 the deleting destructor)."""
    v = {DS_GAME_SLOTS + 0x00: 1, DS_GAME_SLOTS + 0x0C: 0, DS_GAME_SLOTS + 0x10: 0,
         DS_GAME_SLOTS + 0x14: 1, DS_GAME_SLOTS + 0x18: 1,
         0x04: (1, {}, {"com": True}), 0x08: (1, {}, {"com": True})}
    for offset, count, pointers in entries:
        v[offset] = (count, pointers, {"com": True, "returns": ddw_hresult})
    return v


DDW_SETRECT = (DDW_SETRECT_SLOT, 5, {})
DDW_GET_SURFACE_DESC = (0x58, 2, {1: ((0, 4),)})   # its stack local: dwSize
DDW_QUERY = (0x00, 3, {1: 16, 2: ()})              # the IID, a stack out slot
DDW_THROWERS = {**EXCEPTION_CTORS,
                0x00610960: (1, "thiscall", "throw"),   # ThrowOutOfMemory(text, file, line)
                0x006119b0: (1, "thiscall", "throw")}   # ThrowAssertmentFailed(text, file, line)
DDW_SURFACE_EFFECTS = {0x58: ddw_fill_desc(1), 0x00: ddw_write(2, ddw_surface), 0x18: ddw_create_surface}


def ddw_globals(rng, pool, args):
    return [(0x00655330, DDW_VIRTUAL_CODE + DDW_SETRECT_SLOT, 4),
            (0x006552F4, DDW_VIRTUAL_CODE + DDW_SENDMESSAGE_SLOT, 4),
            (0x00655384, DDW_VIRTUAL_CODE + DDW_COCREATE_SLOT, 4),
            (0x007E9168, WORLD_BASE + DDW_APP, 4)]


def ddw_surface_fields(rng, at, null_interface=False):
    """A CSurface at world `at`: fake interfaces, the owner, a version, a
    pixel format or none."""
    fields = [(at + 4, 0 if null_interface else ddw_surface(rng.getrandbits(32)), 4),
              (at + 8, ddw_surface(rng.getrandbits(32)), 4), (at + 0xC, WORLD_BASE + DDW_OWNER, 4),
              (at + 0x10, rng.choice([1, 2, 3, 3, 0, 4, rng.getrandbits(32)]), 4)]
    if rng.random() < 0.5:
        fields.append((at + 0x18, 0, 4))
    return fields


def ddw_world(kind, null_interface=0.0):
    """The object at DDW_OBJ by kind ("draw", "surface", "flip", "primary",
    "primaryflip", "palette", None), the fake COM objects, the owner, a
    second object at DDW_OTHER and the argument buffers."""
    def presets(rng, pool, args):
        fields = [(DDW_COM + 0x10 * k, FAKE_VTABLE, 4) for k in range(9)]
        fields += [(DDW_COM + 0x100, FAKE_VTABLE, 4), (DDW_OWNER, DDW_GAME_VTABLE, 4),
                   (DDW_OWNER + 4, WORLD_BASE + DDW_COM, 4),
                   (DDW_GAMEOBJ, DDW_GAME_VTABLE, 4), (DDW_GAMEOBJ + 0x40, DDW_GAME_VTABLE, 4),
                   (DDW_APP + 0x1C, rng.choice([1, 1, 1, 0, rng.getrandbits(8)]), 1),
                   (DDW_OTHER + 4, ddw_surface(rng.getrandbits(32)), 4),
                   (DDW_DESC + 4, rng.choice([0x1027, 0x21, 1, rng.getrandbits(32)]), 4)]
        nulls = rng.random() < null_interface
        if kind == "draw":
            fields += [(DDW_OBJ, DDW_GAME_VTABLE, 4), (DDW_OBJ + 4, 0 if nulls else WORLD_BASE + DDW_COM, 4),
                       (DDW_OBJ + 8, rng.choice([0, 1, 1, rng.getrandbits(8)]), 1)]
        elif kind == "surface":
            fields += ddw_surface_fields(rng, DDW_OBJ, nulls)
        elif kind == "flip":
            fields += [(DDW_OBJ, 0x0065CDC4, 4),
                       (DDW_OBJ + 8, rng.choice([0, WORLD_BASE + DDW_GAMEOBJ]), 4),
                       (DDW_OBJ + 0xC, rng.choice([0, WORLD_BASE + DDW_GAMEOBJ + 0x40]), 4)]
            fields += ddw_surface_fields(rng, DDW_OBJ + 0x10, nulls)
        elif kind == "primary":
            fields += [(DDW_OBJ, 0x0065CDDC, 4)] + ddw_surface_fields(rng, DDW_OBJ + 4, nulls)
        elif kind == "primaryflip":
            fields += [(DDW_OBJ, 0x0065CDFC, 4), (DDW_OBJ + 4, 0x0065CDF4, 4),
                       (DDW_OBJ + 8, rng.choice([1, 1, 2, 2, 0, 3, rng.getrandbits(32)]), 4),
                       (DDW_OBJ + 0xC, rng.choice([0, WORLD_BASE + DDW_GAMEOBJ]), 4),
                       (DDW_OBJ + 0x10, rng.choice([0, WORLD_BASE + DDW_GAMEOBJ + 0x40]), 4)]
            fields += ddw_surface_fields(rng, DDW_OBJ + 0x14, nulls)
        elif kind == "palette":
            fields += [(DDW_OBJ + 4, rng.choice([0, WORLD_BASE + DDW_COM + 0x100]), 4)]
        return fields
    return presets


def ddw_self(offset=0):
    return lambda rng, pool: WORLD_BASE + DDW_OBJ + offset


def ddw_at(offset, null_chance=0.0):
    return lambda rng, pool: 0 if rng.random() < null_chance else WORLD_BASE + offset


def ddw_flags(rng, pool):
    return rng.choice([0, 0, rng.randrange(0x1000), rng.getrandbits(32)])


def ddw_count(rng, pool):
    return rng.choice([1, 1, 2, 2, 0, 3, rng.getrandbits(32)])


def ddw_size(rng, pool):
    return rng.choice([640, 800, 1024, rng.randrange(5000)])


def ddw_spec(name, address, args, kind, virtuals=None, effects=None, null_interface=0.0, self_offset=0,
             **kwargs):
    return Spec(name, address, "fastcall", [ddw_self(self_offset), anything] + args,
                presets=ddw_world(kind, null_interface), globals=ddw_globals, virtuals=virtuals or ddw_com(),
                virtual_effects=effects, callees=DDW_THROWERS, **kwargs)


def ddw_create(fields):
    """CreateSurface(desc, out, outer); out is the caller's stack local."""
    return (0x18, 4, {**fields, 2: ()})


DDW_SURFACE_DESC_FIELDS = {1: ((0, 16), (0x14, 4), (0x68, 4))}   # offscreen / overlay: size, flags, height, width
DDW_PRIMARY_DESC_FIELDS = {1: ((0, 8), (0x68, 4))}                # primary: size, flags, caps
DDW_FLIP_DESC_FIELDS = {1: ((0, 8), (0x14, 4), (0x68, 4))}        # flip chain: + the back buffer count

SPECS += [
    ddw_spec("DirectX_CDirectDraw_Construct", 0x005bead0, [ddw_at(DDW_OTHER, 0.5)], None),
    ddw_spec("DirectX_CDirectDraw_DeletingDestroy", 0x005beb10, [anything], "draw", null_interface=0.3),
    ddw_spec("DirectX_CDirectDraw_Destroy", 0x005beb30, [], "draw", null_interface=0.3, eax_mask=0),
    ddw_spec("DirectX_CDirectDraw_DDDirectDrawEnumerate", 0x005beb90, [anything, anything], "draw",
             virtuals=ddw_com((DS_PROC_SLOT, 2, {}))),
    ddw_spec("DirectX_CDirectDraw_Connect", 0x005bec00, [ddw_at(DDW_RECT, 0.5)], "draw",
             virtuals=ddw_com((DDW_COCREATE_SLOT, 5, {0: 16, 3: 16}), (0x48, 2, {})),
             effects={DDW_COCREATE_SLOT: ddw_write(4, lambda h: WORLD_BASE + DDW_COM)}),
    ddw_spec("DirectX_CDirectDraw_Disconnect", 0x005bec50, [], "draw"),
    ddw_spec("DirectX_CDirectDraw_DDCreateSurface", 0x005bec80, [ddw_at(DDW_DESC), ddw_at(DDW_OUT)], "draw",
             virtuals=ddw_com(ddw_create({}), DDW_QUERY), effects=DDW_SURFACE_EFFECTS),
    ddw_spec("DirectX_CDirectDraw_DDEnumDisplayModes", 0x005bed10,
             [ddw_flags, ddw_at(DDW_DESC, 0.3), anything, anything], "draw", virtuals=ddw_com((0x20, 5, {}))),
    ddw_spec("DirectX_CDirectDraw_DDRestoreDisplayMode", 0x005bed50, [], "draw", virtuals=ddw_com((0x4C, 1, {}))),
    ddw_spec("DirectX_CDirectDraw_DDGetDisplayMode", 0x005bed70, [ddw_at(DDW_DESC)], "draw",
             virtuals=ddw_com((0x30, 2, {})), effects={0x30: ddw_fill_desc(1)}),
    ddw_spec("DirectX_CDirectDraw_DDGetCaps", 0x005bedb0, [ddw_at(DDW_CAPS_A), ddw_at(DDW_CAPS_B)], "draw",
             virtuals=ddw_com((0x2C, 3, {}))),
    ddw_spec("DirectX_CDirectDraw_DDWaitForVerticalBlank", 0x005bee00, [ddw_flags, anything], "draw",
             virtuals=ddw_com((0x58, 3, {}))),
    ddw_spec("DirectX_CDirectDraw_RestoreSurfaces", 0x005bee30, [], "draw", new_fails=0.1, new_fails_size=0x18),
    ddw_spec("DirectX_CSurface_DDLock", 0x005bf010, [ddw_at(DDW_RECT, 0.3), ddw_at(DDW_DESC), ddw_flags, anything],
             "surface", virtuals=ddw_com((0x64, 5, {}))),
    ddw_spec("DirectX_CSurface_Lock", 0x005bf050, [ddw_at(DDW_RECT, 0.3), ddw_at(DDW_DESC), ddw_flags], "surface",
             virtuals=ddw_com((0x64, 5, {}))),
    ddw_spec("DirectX_CSimpleSurface_DDGetBltStatus", 0x005bf1a0, [ddw_flags], "surface",
             virtuals=ddw_com((0x34, 2, {}))),
    ddw_spec("DirectX_CSimpleSurface_GetInterface", 0x005bf1e0, [], "surface", null_interface=0.3),
    ddw_spec("DirectX_CSurface_UseOwnInterfaceForDrawing", 0x005bf2a0, [], "surface"),
    ddw_spec("DirectX_CSimpleSurface_SetDirectDraw", 0x005bf2b0, [anything], "surface"),
    ddw_spec("DirectX_CSurface_DDGetSurfaceDesc", 0x005bf310, [], "surface",
             virtuals=ddw_com(DDW_GET_SURFACE_DESC, DDW_SETRECT), effects=DDW_SURFACE_EFFECTS),
    ddw_spec("DirectX_CSurface_RestoreIfLost", 0x005bf390, [], "surface",
             virtuals=ddw_com((0x60, 1, {}), (0x6C, 1, {})), new_fails=0.1),
    ddw_spec("DirectX_CSurface_SetCaps", 0x005bf3d0, [anything], "surface"),
    # SetPixelFormat leaves EAX as the caller had it (both callers ignore it).
    ddw_spec("DirectX_CSurface_SetPixelFormat", 0x005bf3e0, [ddw_at(DDW_PF)], "surface", eax_mask=0),
    ddw_spec("DirectX_CSurface_DDRestore", 0x005bf400, [], "surface", virtuals=ddw_com((0x6C, 1, {})),
             new_fails=0.1),
    ddw_spec("DirectX_CFlipSurface_Construct", 0x005bf5c0, [ddw_count, one_of(0, 1, 2, 0x100)], "flip"),
    ddw_spec("DirectX_CFlipSurface_DeletingDestroy", 0x005bf610, [anything], "flip", null_interface=0.3,
             self_offset=0x10),
    # The destructors' EAX is what their last call left; the deleting
    # destructors (their only callers) overwrite it.
    ddw_spec("DirectX_CFlipSurface_Destroy", 0x005bf640, [], "flip", self_offset=0x10, eax_mask=0),
    ddw_spec("DirectX_CFlipSurface_GetBackBuffer", 0x005bf670, [], "flip", new_fails=0.2),
    ddw_spec("DirectX_CFlipSurface_Flip", 0x005bf710, [one_of(0, WORLD_BASE + DDW_COM + 0x10)], "flip",
             virtuals=ddw_com((0x2C, 3, {}))),
    ddw_spec("DirectX_CFlipSurface_DDFlip", 0x005bf740, [one_of(0, WORLD_BASE + DDW_COM + 0x10), ddw_flags], "flip",
             virtuals=ddw_com((0x2C, 3, {}))),
    ddw_spec("DirectX_CPrimarySurface_Construct", 0x005bf790, [one_of(0, 1, 2, 0x100)], "primary"),
    ddw_spec("DirectX_CPrimarySurface_DeletingDestroy", 0x005bf7d0, [anything], "primary", null_interface=0.3,
             self_offset=4),
    ddw_spec("DirectX_CPrimarySurface_Create", 0x005bf800, [], "primary",
             virtuals=ddw_com(ddw_create(DDW_PRIMARY_DESC_FIELDS), DDW_QUERY, DDW_GET_SURFACE_DESC, DDW_SETRECT),
             effects=DDW_SURFACE_EFFECTS),
    ddw_spec("DirectX_CPrimaryFlipSurface_Construct", 0x005bf890, [ddw_count, one_of(0, 1, 2, 0x100)],
             "primaryflip"),
    ddw_spec("DirectX_CPrimaryFlipSurface_DeletingDestroy", 0x005bf930, [anything], "primaryflip",
             null_interface=0.3, self_offset=0x14),
    ddw_spec("DirectX_CPrimaryFlipSurface_Destroy", 0x005bf960, [], "primaryflip", self_offset=0x14, eax_mask=0),
    ddw_spec("DirectX_CPrimaryFlipSurface_Create", 0x005bf9b0, [], "primaryflip",
             virtuals=ddw_com(ddw_create(DDW_FLIP_DESC_FIELDS), DDW_QUERY, DDW_GET_SURFACE_DESC, DDW_SETRECT,
                              (0x30, 3, {1: 4})),   # GetAttachedSurface(&caps, &base->surface)
             effects={**DDW_SURFACE_EFFECTS, 0x30: ddw_write(2, ddw_surface)}),
    ddw_spec("DirectX_COffscreenSurface_Construct", 0x005bfab0, [], None),
    ddw_spec("DirectX_COffscreenSurface_DeletingDestroy", 0x005bfad0, [anything], "surface", null_interface=0.3),
    ddw_spec("DirectX_COffscreenSurface_Create", 0x005bfaf0, [ddw_size, ddw_size], "surface",
             virtuals=ddw_com(ddw_create(DDW_SURFACE_DESC_FIELDS), DDW_QUERY, DDW_GET_SURFACE_DESC, DDW_SETRECT),
             effects=DDW_SURFACE_EFFECTS),
    ddw_spec("DirectX_COffscreenSurface_RestoreIfLost", 0x005bfb80, [], "surface",
             virtuals=ddw_com((0x60, 1, {}), (0x6C, 1, {})), new_fails=0.1),
    ddw_spec("DirectX_COverlaySurface_Construct", 0x005bfba0, [], None),
    ddw_spec("DirectX_COverlaySurface_Create", 0x005bfbc0, [ddw_size, ddw_size], "surface",
             virtuals=ddw_com(ddw_create(DDW_SURFACE_DESC_FIELDS), DDW_QUERY, DDW_GET_SURFACE_DESC, DDW_SETRECT),
             effects=DDW_SURFACE_EFFECTS),
    ddw_spec("DirectX_COverlaySurface_Hide", 0x005bfc50, [ddw_at(DDW_OTHER, 0.4)], "surface",
             virtuals=ddw_com((0x84, 6, {}))),
    ddw_spec("DirectX_COverlaySurface_DDUpdateOverlay", 0x005bfc70,
             [ddw_at(DDW_RECT, 0.3), ddw_at(DDW_OTHER, 0.4), ddw_at(DDW_RECT, 0.3), ddw_flags, anything], "surface",
             virtuals=ddw_com((0x84, 6, {}))),
    # cover_until: Ghidra's function also takes in the padding and the
    # static initializer that follow it (0x5bfd80).
    ddw_spec("DirectX_CPalette_Release", 0x005bfd60, [], "palette", cover_until=0x005bfd79),
]


# -- wip/ddraw-render: CGameDrawer members (directx_drawer.cpp) --------------
# The drawer at DDW_OBJ (game vtable, the IDirectDraw2 at +4), its surface
# list (+0x10) and palette list (+0x28) with the list vtable FAKE_VTABLE +
# DGD_LIST_SLOTS (1 destroy a link, 2 compare), no allocator (the
# emulator's operator new), circles of links at DGD_LINKS to surface
# objects (game vtable: 0 delete, 1 Release, 2 RestoreIfLost) or to the
# primary's CSurface part (a CPrimarySurface at DGD_PRIMARY, real vbtable),
# and CPalettes at DGD_PALETTES (+4 a COM palette or 0).
DGD_LIST_SLOTS = 0x300
DGD_LINKS = 0x500
DGD_SURFACES = 0x600
DGD_PRIMARY = 0x6C0
DGD_PALETTES = 0x700
DGD_MODE_A = 0x280
DGD_MODE_B = DDW_RECT


def dgd_circle(fields, list_at, link_at, elements):
    fields += [(list_at, FAKE_VTABLE + DGD_LIST_SLOTS, 4), (list_at + 0xC, len(elements), 4), (list_at + 0x10, 0, 4)]
    for k, element in enumerate(elements):
        nxt = WORLD_BASE + link_at + 8 * ((k + 1) % len(elements))
        fields += [(link_at + 8 * k, nxt, 4), (link_at + 8 * k + 4, element, 4)]
    fields.append((list_at + 8, WORLD_BASE + link_at + 8 * (len(elements) - 1) if elements else 0, 4))


def dgd_world(rng, pool, args):
    fields = ddw_world("draw")(rng, pool, args)
    surfaces = [WORLD_BASE + DGD_SURFACES + 0x10 * k for k in range(rng.randrange(7))]
    if rng.random() < 0.5:
        surfaces.insert(rng.randrange(len(surfaces) + 1), WORLD_BASE + DGD_PRIMARY + 4)
    dgd_circle(fields, DDW_OBJ + 0x10, DGD_LINKS, surfaces)
    palettes = [WORLD_BASE + DGD_PALETTES + 8 * k for k in range(rng.randrange(6))]
    dgd_circle(fields, DDW_OBJ + 0x28, DGD_LINKS + 0x80, palettes)
    fields += [(DGD_SURFACES + 0x10 * k, DDW_GAME_VTABLE, 4) for k in range(7)]
    fields += [(DGD_PALETTES + 8 * k + 4, rng.choice([0, WORLD_BASE + DDW_COM + 0x100]), 4) for k in range(6)]
    fields += [(DGD_PRIMARY, 0x0065CDDC, 4), (DGD_PRIMARY + 4, DDW_GAME_VTABLE, 4),
               (DDW_OBJ + 0x78, rng.choice([0, WORLD_BASE + DGD_PRIMARY]), 4),
               (DDW_OBJ + 0x64, rng.choice([0, 0, WORLD_BASE + SCRATCH]), 4)]
    return fields


def dgd_surface_arg(rng, pool):
    return rng.choice([WORLD_BASE + DGD_SURFACES + 0x10 * rng.randrange(8), WORLD_BASE + DGD_PRIMARY + 4, 0,
                       rng.getrandbits(32)])


DGD_VIRTUALS = {**ddw_com(), DS_GAME_SLOTS + 0x04: 0, DS_GAME_SLOTS + 0x08: 0, DS_GAME_SLOTS + 0x28: 0,
                DS_GAME_SLOTS + 0x2C: 1, DGD_LIST_SLOTS + 0x04: 1, DGD_LIST_SLOTS + 0x08: (2, {0: 4, 1: 4})}


def dgd_spec(name, address, args, self_at=DDW_OBJ, virtuals=None, **kwargs):
    return Spec(name, address, "fastcall", [one_of(WORLD_BASE + self_at), anything] + args, presets=dgd_world,
                globals=ddw_globals, virtuals=virtuals or DGD_VIRTUALS, callees=DDW_THROWERS, **kwargs)


SPECS += [
    dgd_spec("DirectX_CDisplayMode_Construct", 0x005b6c30, [ddw_at(DGD_MODE_B, 0.3)], self_at=DGD_MODE_A),
    dgd_spec("DirectX_CDisplayMode_IsEqual", 0x005b6c60, [ddw_at(DGD_MODE_B)], self_at=DGD_MODE_A),
    dgd_spec("ReturnThis", 0x005b6c20, [], cover_until=0x005b6c23),
    dgd_spec("DirectX_CGameDrawer_Initialize", 0x005b6ed0, [anything],
             virtuals={**DGD_VIRTUALS, DS_PROC_SLOT: (2, {}, {"com": True, "returns": ddw_hresult})}),
    dgd_spec("DirectX_CGameDrawer_CreateOnDevice", 0x005b6f60, [ddw_at(DDW_RECT, 0.5)],
             virtuals={**DGD_VIRTUALS, DDW_COCREATE_SLOT: (5, {0: 16, 3: 16}, {"com": True, "returns": ddw_hresult}),
                       0x48: (2, {}, {"com": True, "returns": ddw_hresult}),
                       0x20: (5, {}, {"com": True, "returns": ddw_hresult})},
             virtual_effects={DDW_COCREATE_SLOT: ddw_write(4, lambda h: WORLD_BASE + DDW_COM)}),
    dgd_spec("DirectX_CGameDrawer_Create", 0x005b6f90, [ddw_at(DDW_RECT, 0.5)],
             virtuals={**DGD_VIRTUALS, DDW_COCREATE_SLOT: (5, {0: 16, 3: 16}, {"com": True, "returns": ddw_hresult}),
                       0x48: (2, {}, {"com": True, "returns": ddw_hresult}),
                       0x20: (5, {}, {"com": True, "returns": ddw_hresult})},
             virtual_effects={DDW_COCREATE_SLOT: ddw_write(4, lambda h: WORLD_BASE + DDW_COM)}),
    # Destroy's EAX is what operator delete left (callers ignore it).
    dgd_spec("DirectX_CGameDrawer_Destroy", 0x005b6fc0, [], eax_mask=0),
    dgd_spec("DirectX_CGameDrawer_AddSurface", 0x005b7000, [dgd_surface_arg], new_fails=0.2),
    # 0x5b704e: a null primary (both then crash reading its vbptr at 0x5b708b).
    dgd_spec("DirectX_CGameDrawer_SetPrimarySurface", 0x005b7040, [one_of(WORLD_BASE + DGD_PRIMARY)], new_fails=0.2,
             exempt_blocks=(0x5b704e,)),
    dgd_spec("DirectX_CGameDrawer_RemoveSurface", 0x005b70c0, [dgd_surface_arg]),
    # SetMemberByte7CToOne leaves EAX as the caller had it.
    dgd_spec("SetMemberByte7CToOne", 0x005b7160, [anything], eax_mask=0),
    # The exempt blocks are the iterators' "no list or no current link"
    # fallbacks: dead, since a successful += leaves both set.
    dgd_spec("DirectX_CGameDrawer_ReleaseSurfaces", 0x005b7170, [anything], exempt_blocks=(0x5b71e0, 0x5b7204)),
    dgd_spec("WCListIterBase_Construct", 0x005b72b0, [anything], self_at=DGD_MODE_A),
    dgd_spec("DirectX_CGameDrawer_RestoreSurfaces", 0x005b72e0, [], exempt_blocks=(0x5b7357,)),
    dgd_spec("DirectX_CGameDrawer_ReleasePalettes", 0x005b7390, [anything], exempt_blocks=(0x5b73fb,)),
    # RequestFullRedraw leaves EAX as the caller had it.
    dgd_spec("DirectX_CGameDrawThread_RequestFullRedraw", 0x005b6730, [], eax_mask=0),
]


# -- session 58 (wip/framework-errors): CHResult, CTimeout, CMMException,
# CMCIException, their throwers and the message destructors
# (win32_throwers.cpp) ----------------------------------------------------
#
# Constructors build the object at world offset 0 from the message in
# STRING_OTHER and (CHResult) the location in STRING_OTHER + 0x10
# (member_strings); the Describe functions read an object at world 0 (its
# message at +8 from string_fields, the code at +0x18) and write the result
# into STRING_RESULT. FormatMessageA, LocalFree and mciGetErrorStringA are
# API probes (emu.py). The throwers end in _CxxThrowException (compared).

THROWER_CODES = (0, 1, 2, 5, 0x57, 0x106, 0x113, 0x101, 0x80070002, 0x80070005, 0x8007000E, 0x80004005,
                 0x88780078, 0x00070005, 0x1FFF0000, 0x80060005)


def thrower_code(rng, pool) -> int:
    roll = rng.random()
    if roll < 0.25:
        return rng.getrandbits(32)
    if roll < 0.5:
        return (rng.getrandbits(32) & 0xE000FFFF) | 0x70000  # other Win32 HRESULTs
    return rng.choice(THROWER_CODES)


def code_exception(rng, pool, args):
    """An exception object at world 0: its message (+8) and code (+0x18)."""
    fields, _ = string_fields(rng, 0x08, STRING_SELF_BUFFER)
    return fields + [(0x18, thrower_code(rng, pool), 4)]


def message_or_location(rng, pool) -> int:
    return STRING_OTHER_PTR + 0x10 if rng.random() < 0.1 else STRING_OTHER_PTR


THROWER_STRINGS = member_strings((), (0x0, 0x10))


def message_destructor_spec(name, address):
    # Dead: start + 0x9d is the _Eos after erase(0, npos) (the new length is
    # 0, for which _Grow returns false); start + 0xbf decrements the count of
    # a buffer the split has just made unshared (as ~CDirectSoundException).
    return Spec(name, address, "thiscall", [], eax_mask=0, presets=member_strings((0x08,)),
                exempt_blocks=(address + 0x9d, address + 0xbf))


SPECS += [
    # Dead: 0x6132ed is the _Eos after erase(0, npos) of the description
    # temporary (the new length is 0, for which _Grow returns false).
    Spec("Win32_CHResult_Construct", 0x00613220, "thiscall",
         [message_or_location, thrower_code, one_of(STRING_OTHER_PTR + 0x10), mostly_derived],
         presets=THROWER_STRINGS, exempt_blocks=(0x006132ed,)),
    # ignore_bytes: the result's allocator byte is an uninitialized local of
    # the original (never read). Dead: 0x6137e5 is the _Eos after erase(0,
    # npos) of the system text (as above).
    Spec("Win32_CHResult_Describe", 0x006134d0, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=code_exception, ignore_bytes=(STRING_RESULT,), exempt_blocks=(0x006137e5,)),
    # Dead (both strings): the _Eos after erase(0, npos) (0x6133dd,
    # 0x61346f) and the count decrement after the split (0x6133ff, 0x613491).
    Spec("Win32_CHResult_Destroy", 0x00613340, "thiscall", [], eax_mask=0, presets=member_strings((0x08, 0x1c)),
         exempt_blocks=(0x006133dd, 0x006133ff, 0x0061346f, 0x00613491)),
    Spec("Win32_ThrowWin32Error", 0x00610de0, "fastcall",
         [text_or_null, text_or_null, anything, thrower_code, one_of(STRING_OTHER_PTR + 0x10)],
         presets=THROWER_STRINGS, new_fails=0.2, new_fails_size=0x2c),
    Spec("Win32_ThrowHResult", 0x00611050, "fastcall",
         [text_or_null, text_or_null, anything, thrower_code, one_of(STRING_OTHER_PTR + 0x10)],
         presets=THROWER_STRINGS, new_fails=0.2, new_fails_size=0x2c),
    Spec("Win32_CTimeout_Construct", 0x00615120, "thiscall", [message_ptr, mostly_derived], presets=string_world),
    Spec("Win32_ThrowTimeout", 0x00610ba0, "fastcall", [text_or_null, text_or_null, anything],
         presets=string_world, new_fails=0.2, new_fails_size=0x18),
    # Dead (both constructors): the inlined split of the description
    # temporary (0x613abb..0x613b0f, 0x61403b..0x61408f: Describe's result
    # never shares its buffer, its count is 0), the _Eos after erase(0, npos)
    # (0x613b47, 0x6140c7) and the count decrement (0x613b68 / 0x613b6c,
    # 0x6140e8 / 0x6140ec).
    Spec("Win32_CMMException_Construct", 0x00613a40, "thiscall", [message_ptr, thrower_code, mostly_derived],
         presets=string_world,
         exempt_blocks=(0x00613abb, 0x00613ac0, 0x00613aef, 0x00613b0f, 0x00613b47, 0x00613b68, 0x00613b6c)),
    Spec("Win32_CMMException_Describe", 0x00613cd0, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=code_exception, ignore_bytes=(STRING_RESULT,)),
    Spec("Win32_ThrowMMError", 0x006112a0, "fastcall", [text_or_null, text_or_null, anything, thrower_code],
         presets=string_world, new_fails=0.2, new_fails_size=0x1c),
    Spec("Win32_CMCIException_Construct", 0x00613fc0, "thiscall", [message_ptr, thrower_code, mostly_derived],
         presets=string_world,
         exempt_blocks=(0x0061403b, 0x00614040, 0x0061406f, 0x0061408f, 0x006140c7, 0x006140e8, 0x006140ec)),
    Spec("Win32_CMCIException_Describe", 0x00614250, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=code_exception, ignore_bytes=(STRING_RESULT,)),
    Spec("Win32_ThrowMCIError", 0x006114f0, "fastcall", [text_or_null, text_or_null, anything, thrower_code],
         presets=string_world, new_fails=0.2, new_fails_size=0x1c),
    message_destructor_spec("Win32_CRunTimeError_Destroy", 0x005b93c0),
    message_destructor_spec("Win32_COutOfMemory_Destroy", 0x00612790),
    message_destructor_spec("Win32_CProgrammingError_Destroy", 0x00612c80),
    message_destructor_spec("Win32_CAssertmentFailed_Destroy", 0x00612d80),
    message_destructor_spec("Win32_CTimeout_Destroy", 0x00615170),
    message_destructor_spec("Win32_CMMException_Destroy", 0x00613bd0),
    message_destructor_spec("Win32_CStructuredException_Destroy", 0x0060e490),
    message_destructor_spec("DirectX_CDirectInputException_Destroy", 0x005b94c0),
    message_destructor_spec("DirectX_CDirectDrawException_Destroy", 0x005be3e0),
    message_destructor_spec("DirectX_CDirectPlayException_Destroy", 0x005c0010),
    message_destructor_spec("Lobby_CDataError_Destroy", 0x00600400),
    # ~CException splits with _Grow + memcpy instead of assign (other block
    # addresses). Dead: the _Eos after erase (0x6108f7) and the count
    # decrement after the split (0x610919), as above.
    Spec("Win32_CException_Destroy", 0x00610830, "thiscall", [], eax_mask=0, presets=member_strings((0x08,)),
         exempt_blocks=(0x006108f7, 0x00610919)),
]


# -- session 58 (wip/framework-errors): the synchronization objects, threads
# and windows (win32_synchronize.cpp) --------------------------------------
#
# The object at world 0: handle at +8 (the CWin32Handle at +4), HWND at +4;
# a CMessagedThread's virtual-base table at SYNC_VBTABLE (entry 1 = the
# offset of its CThread part, whose thread id is at +0x14). The Windows
# calls are API probes (emu.py); a failed VERIFY runs the real
# ThrowWin32Error / ThrowTimeout and ends in _CxxThrowException. Their
# names are null or STRING_TEXT; 0x7a7868 (the location's "") is sometimes
# not empty (empty_text).
SYNC_VBTABLE = SCRATCH + 20 * SCRATCH_SLOT_BYTES


def sync_object(rng, pool, args):
    offset = rng.choice([0x10, 0x20, 0x24, 0x34])
    return [(0x04, rng.choice([0, 1, rng.getrandbits(32)]), 4), (0x08, rng.getrandbits(32), 4),
            (0x00, WORLD_BASE + SYNC_VBTABLE, 4), (SYNC_VBTABLE + 4, offset, 4),
            (offset + 0x14, rng.getrandbits(32), 4)]


def sync_name(rng, pool) -> int:
    return rng.choice([0, STRING_TEXT_PTR])


def sync_world(rng, pool, args):
    """sync_object and a name text at STRING_TEXT."""
    text = [(STRING_TEXT + i, rng.randint(1, 255), 1) for i in range(rng.randrange(0x20))]
    return sync_object(rng, pool, args) + text + [(STRING_TEXT + len(text), 0, 1)]


def sync_spec(name, address, args, exempt=()):
    # Dead (exempt): the first block is the free of the location's first
    # temporary, whose buffer (when 0x7a7868 is not empty) is always shared
    # with the location by then (count >= 1); the others are the code after
    # the ThrowWin32Error / ThrowTimeout call, which destroys the location
    # as if the throw returned.
    return Spec(name, address, "thiscall", args, presets=sync_world, globals=empty_text, exempt_blocks=exempt)


SPECS += [
    sync_spec("Win32_CEvent_Set", 0x0042cf80, [],
              exempt=(0x42d076, 0x42d0cd, 0x42d0e4, 0x42d0eb, 0x42d0f0, 0x42d118, 0x42d120, 0x42d122, 0x42d126, 0x42d14c, 0x42d158, 0x42d15c, 0x42d161, 0x42d16b, 0x42d16f, 0x42d185)),
    sync_spec("Win32_CEvent_Reset", 0x00523110, [],
              exempt=(0x523206, 0x52325d, 0x523274, 0x52327b, 0x523280, 0x5232a8, 0x5232b0, 0x5232b2, 0x5232b6, 0x5232dc, 0x5232e8, 0x5232ec, 0x5232f1, 0x5232fb, 0x5232ff, 0x523315)),
    sync_spec("Win32_CEvent_Pulse", 0x0047fc80, [],
              exempt=(0x47fd71, 0x47fdc8, 0x47fddf, 0x47fde6, 0x47fdeb, 0x47fe13, 0x47fe1b, 0x47fe1d, 0x47fe21, 0x47fe47, 0x47fe53, 0x47fe57, 0x47fe5c, 0x47fe66, 0x47fe6a, 0x47fe80)),
    sync_spec("Win32_CSemaphore_Release", 0x0053e830, [anything],
              exempt=(0x53e92d, 0x53e984, 0x53e99b, 0x53e9a2, 0x53e9a7, 0x53e9cf, 0x53e9d7, 0x53e9d9, 0x53e9dd, 0x53ea03, 0x53ea0f, 0x53ea13, 0x53ea18, 0x53ea22, 0x53ea26, 0x53ea3e)),
    sync_spec("Win32_CEvent_Create", 0x0060c530, [anything, anything, anything, sync_name],
              exempt=(0x60c63e, 0x60c695, 0x60c6ac, 0x60c6b3, 0x60c6b8, 0x60c6e0, 0x60c6e8, 0x60c6ea, 0x60c6ee, 0x60c714, 0x60c720, 0x60c724, 0x60c729, 0x60c733, 0x60c737, 0x60c73d)),
    sync_spec("Win32_CSemaphore_Create", 0x0060c7a0, [anything, anything, anything, sync_name],
              exempt=(0x60c8ae, 0x60c905, 0x60c91c, 0x60c923, 0x60c928, 0x60c950, 0x60c958, 0x60c95a, 0x60c95e, 0x60c984, 0x60c990, 0x60c994, 0x60c999, 0x60c9a3, 0x60c9a7, 0x60c9ad)),
    sync_spec("Win32_CMutex_Create", 0x0060ca30, [anything, anything, sync_name],
              exempt=(0x60cb3a, 0x60cb91, 0x60cba8, 0x60cbaf, 0x60cbb4, 0x60cbdc, 0x60cbe4, 0x60cbe6, 0x60cbea, 0x60cc10, 0x60cc1c, 0x60cc20, 0x60cc25, 0x60cc2f, 0x60cc33, 0x60cc39)),
    sync_spec("Win32_CWin32Waitable_Wait", 0x0060c060, [anything, lambda rng, pool: rng.choice([0, 1, 0x100, 0x101])],
              exempt=(0x60c1a3, 0x60c1f3, 0x60c20a, 0x60c211, 0x60c216, 0x60c23e, 0x60c246, 0x60c248, 0x60c24c, 0x60c272, 0x60c27e, 0x60c282, 0x60c287, 0x60c291, 0x60c295, 0x60c29b)),
    sync_spec("Win32_CThread_SetPriority", 0x0060e9d0, [anything],
              exempt=(0x60eacb, 0x60eb22, 0x60eb39, 0x60eb40, 0x60eb45, 0x60eb6d, 0x60eb75, 0x60eb77, 0x60eb7b, 0x60eba1, 0x60ebad, 0x60ebb3, 0x60ebb8, 0x60ebc2, 0x60ebc6, 0x60ebde)),
    sync_spec("Win32_CMessagedThread_PostThreadMessage", 0x0060f480, [anything, anything, anything],
              exempt=(0x60f59d, 0x60f5f4, 0x60f5fb, 0x60f600, 0x60f62a, 0x60f633, 0x60f635, 0x60f639, 0x60f65f, 0x60f66b, 0x60f671, 0x60f675, 0x60f67f, 0x60f683, 0x60f689)),
    sync_spec("Win32_CWindowBase_PostMessage", 0x0060f9b0, [anything, anything, anything],
              exempt=(0x60fab6, 0x60fb0d, 0x60fb24, 0x60fb2b, 0x60fb30, 0x60fb58, 0x60fb60, 0x60fb62, 0x60fb66, 0x60fb8c, 0x60fb98, 0x60fb9c, 0x60fba1, 0x60fbab, 0x60fbaf, 0x60fbc7)),
    sync_spec("Win32_CWindowBase_Destroy", 0x0060fbf0, [],
              exempt=(0x60fced, 0x60fd44, 0x60fd5a, 0x60fd61, 0x60fd66, 0x60fd92, 0x60fd9a, 0x60fd9c, 0x60fda0, 0x60fdc6, 0x60fdd2, 0x60fdda, 0x60fde0, 0x60fdea, 0x60fdee, 0x60fe08, 0x60fe11)),
]


# -- session 58: CCDAudio Open, Close, DoPlay, ~CCDAudio (win32_cdaudio.cpp).
# mciSendCommandA is the emu.py API probe here (the import slot is left
# alone; it records the parameter blocks by message), so a failed check
# runs the real ThrowMCIError and ends in _CxxThrowException.
SPECS += [
    Spec("Win32_CCDAudio_Open", 0x00615740, "thiscall", [], presets=cd_world),
    Spec("Win32_CCDAudio_Close", 0x00615e20, "thiscall", [], presets=cd_world),
    Spec("Win32_CCDAudio_DoPlay", 0x00615ee0, "thiscall",
         [lambda rng, pool: rng.choice([0xFFFFFFFF, 1, 5, 99, rng.getrandbits(32)]),
          lambda rng, pool: rng.choice([0xFFFFFFFF, 0, 3, 50, 126, 127, rng.getrandbits(32)])], presets=cd_world),
    Spec("Win32_CCDAudio_Destroy", 0x00615500, "thiscall", [], presets=cd_world),
]
