"""Emulator specs, part `logic_events`: CLogicThread::ProcessEvents 0x524fa0
(src/s3re/game/logic/events/).

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.

Bounded specs of the 65 KB function (policy for large functions; the
in-process tests and the whole-game checks carry the rest). The fake
CLogicThread (LOGIC_THREAD) gets its input queue (+0x3c0, holding the
case's events, 5 dwords each) and the logic -> network queue (+0x3c4,
collecting the pushed commands); the game's own MessageQueue::Pop and
Push run on them (critical sections probed, Push's SetEvent stubbed, as
for ExecuteCommands). The geometry globals (cell size, draw origin,
sidebar, minimap) are drawn per case; the mouse position (+0x44/+0x48)
is in the panel, the minimap or the map.

One spec per part of the function (PE_FOCUS): "general" draws every kind
of event; "menuN" puts the click or the hover event into the panel with
menu N open, so the menu's handler is reached often. Each part adds its
world fields with PE_MENU_PRESETS[N] (rng, fields) and its probed
callees to PE_CALLEES.
"""

PE_EVENTS = LOGIC_THREAD + 0x1000      # the input queue (MessageQueue)
PE_TO_NET = LOGIC_THREAD + 0x1040      # the logic -> network queue
PE_EVENT_HANDLE = LOGIC_THREAD + 0x1080
PE_EVENT_RING = LOGIC_THREAD + 0x2000
PE_NET_RING = LOGIC_THREAD + 0x3000
PE_RING_DWORDS = 1024
PE_SET_EVENT_CALL = 0x00430b5e
PE_SET_EVENT_STUB = int.from_bytes(b"\x59\xb8\x01\x00\x00\x00", "little")  # pop ecx; mov eax, 1

# Event types (UiEventType) with weights for the general spec.
PE_EVENT_TYPES = [(0x10, 30), (0x11, 4), (0x16, 14), (0x01, 18), (0x12, 8), (0x13, 4), (0x15, 8), (0x17, 6),
                  (0x03, 3), (0x20, 3), (0x00, 2)]

# The thread's fields (offsets in CLogicThread).
PE_MOUSE_X = 0x44
PE_MOUSE_Y = 0x48
PE_IDLE = 0x74
PE_PAUSED = 0x220
PE_CHAT_OPEN = 0x408

# Callees probed (address -> as Spec.callees). The verified UI helpers
# that only touch the thread and the world run; these are probed because
# they draw, load, or reach code the emulator can't run (sound, files,
# the network): recorded with their arguments.
PE_CALLEES = {
    0x0060d670: (0, "thiscall", "probe"), 0x0060d680: (0, "thiscall", "probe"),  # Enter/LeaveCriticalSection
    0x004fb650: (0, "thiscall", "probe"),   # CGameData::BuildMinimap (verified; slow)
    0x004fee90: (1, "thiscall", "probe"),   # CGameData::GoToNotification (verified; spawns an effect)
    0x0053e720: (0, "thiscall", "probe"),   # CLogicThread::FinishGame (verified; posts a message)
    0x0053f5f0: (0, "thiscall", "probe"),   # CLogicThread::PlayNextMusicTrack (verified; sound)
    0x0051e2e0: (1, "thiscall", "probe"),   # CLogicThread::ChatInputChar (logic-r8)
    0x0051e160: (1, "thiscall", lambda h: h & 1),  # CLogicThread::ChatInputKey (verified) -> AL
    0x0060f480: (3, "thiscall", "probe"),   # Win32::CMessagedThread::PostThreadMessage (verified)
    0x004318d0: (0, "thiscall", lambda h: h & 1),  # GameSettings::IsMultiplayer (verified) -> AL
    0x004ff0a0: (2, "thiscall", "probe"),   # CGameData::PickHitArea (verified)
    0x004ff170: (2, "thiscall", "probe"),   # CGameData::TileAtScreenPoint (verified)
    0x0051b560: (2, "thiscall", "probe"),   # CGameData::CenterCameraOnTile (verified)
    0x0051b4d0: (0, "thiscall", "probe"),   # CGameData::ClampCamera (verified)
    0x00542630: (0, "thiscall", "probe"),   # CLogicThread::ClearSelection (verified)
}

# Per focus: more world/thread fields; filled by each part below.
PE_MENU_PRESETS: dict = {}
# Per focus: more globals (rng) -> [(address, value, size)].
PE_MENU_GLOBALS: dict = {}


def pe_event(type_: int, rng) -> list:
    """The 5 dwords of an event of `type_` with random contents."""
    data = bytearray(rng.getrandbits(8) for _ in range(20))
    data[0] = type_
    if type_ == 0x01:  # a key: mostly the ones the switch knows
        data[4] = rng.choice([9, 13, 0x1b, 0x20, 0x24, 0x25, 0x26, 0x27, 0x28, 0x31, 0x35, 0x39, 0x41, 0x42, 0x43,
                              0x44, 0x45, 0x46, 0x47, 0x49, 0x4a, 0x4b, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
                              0x56, 0x57, 0x58, 0x59, 0x5a, 0x6b, 0x6d, 0x70, 0x71, 0x72, 0x74, 0x75, 0x76, 0x77,
                              0x7b, rng.getrandbits(8)])
        data[1] = rng.choice([0, 0, 1, 2, 4, rng.getrandbits(8)])
    elif type_ == 0x20:
        data[2] = rng.choice([1, 1, 0])
    elif type_ in (0x16, 0x17):
        if rng.random() < 0.7:
            data[8:10] = rng.randrange(1024).to_bytes(2, "little")
            data[10:12] = rng.randrange(768).to_bytes(2, "little")
    return [int.from_bytes(bytes(data[i:i + 4]), "little") for i in range(0, 20, 4)]


def pe_queue(offset: int, ring: int, count: int) -> list:
    return [(offset, FAKE_VTABLE, 4), (offset + 0x14, WORLD_BASE + ring, 4),
            (offset + 0x18, PE_RING_DWORDS, 4), (offset + 0x1c, 0, 4), (offset + 0x20, count, 4),
            (offset + 0x24, count, 4), (offset + 0x28, WORLD_BASE + PE_EVENT_HANDLE, 4),
            (offset + 0x2c, WORLD_BASE + PE_EVENT_HANDLE + 0x20, 4), (offset + 0x30, 0, 4)]


# The geometry of a case: display mode 1..3 as Render::SetScreenLayout sets
# it (cell size, draw origin, sidebar, minimap), with some noise.
PE_LAYOUTS = [  # (display w, h, sidebar, minimap width, 2c, 30, 34, cell)
    (640, 480, 136, 128, 5, 10, 142, 3), (800, 600, 176, 128, 8, 5, 170, 4), (1024, 768, 216, 128, 6, 8, 224, 5)]


def pe_layout(rng):
    mode = rng.randrange(3)
    width, height, sidebar, minimap, l2c, l30, l34, cell = PE_LAYOUTS[mode]
    origin_x, origin_y = rng.randrange(4), l34 + rng.randrange(8)
    return mode, (width, height, sidebar, minimap, l2c, l30, l34, cell, origin_x, origin_y)


def pe_layout_globals(layout, mode) -> list:
    width, height, sidebar, minimap, l2c, l30, l34, cell, origin_x, origin_y = layout
    return [(0x00730F08, mode + 1, 4), (0x00730F0C, width, 4), (0x00730F10, height, 4),
            (0x00730F24, sidebar, 4), (0x00730F28, minimap, 4), (0x00730F2C, l2c, 4), (0x00730F30, l30, 4),
            (0x00730F34, l34, 4), (0x00730F38, cell, 4), (0x00730F3C, origin_x, 4), (0x00730F40, origin_y, 4)]


def pe_mouse(rng, layout, where: str) -> tuple:
    width, height, sidebar, minimap, l2c, l30, l34, cell, origin_x, origin_y = layout
    if where == "panel":
        return origin_x + rng.randrange(40 * cell + 2), origin_y + rng.randrange(106 * cell + 2)
    if where == "minimap":
        return rng.randrange(sidebar + 20), rng.randrange(l34)
    if where == "map":
        return sidebar + rng.randrange(width - sidebar), rng.randrange(height)
    return rng.choice([0, width - 1, rng.randrange(width)]), rng.choice([0, height - 1, rng.randrange(height)])


# The case's geometry, drawn by the presets (which run first) and set as
# globals by pe_globals_for.
_pe_case: dict = {}


def _pe_debug(focus, menu, mouse, idle, events, dwords) -> None:
    """With PE_DEBUG set (development only): prints each case's input, in
    case order (run with --jobs 1)."""
    import os as _os
    if not _os.environ.get("PE_DEBUG"):
        return
    _pe_case["n"] = _pe_case.get("n", -1) + 1
    print(f"  pe case {_pe_case['n']}: focus {focus} menu {menu} mouse {mouse} idle {idle} events "
          + " ".join(f"{t:#x}" for t in events) + " dwords " + " ".join(f"{d:08x}" for d in dwords))


def pe_presets_for(focus: str):
    def presets(rng, pool, args):
        mode, layout = pe_layout(rng)
        _pe_case["layout"] = (mode, layout)
        fields = []
        if focus == "general":
            where = rng.choice(["panel", "panel", "minimap", "map", "edge"])
            menu = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 11, rng.randrange(14)])
            count = rng.randrange(5)
            types = [t for t, w in PE_EVENT_TYPES for _ in range(w)]
            events = [rng.choice(types) for _ in range(count)]
        else:
            menu = int(focus[4:])
            where = "panel" if rng.random() < 0.9 else rng.choice(["minimap", "map"])
            events = [rng.choice([0x10, 0x10, 0x10, 0x11])] if rng.random() < 0.8 else []
            if rng.random() < 0.2:
                events.append(rng.choice([0x10, 0x11]))
        x, y = pe_mouse(rng, layout, where)
        idle = rng.choice([0, 3, 9, 9, 10, 11, rng.getrandbits(32)]) if not events else rng.choice([0, 9, 10, 20])
        dwords = []
        for t in events:
            dwords += pe_event(t, rng)
        t = LOGIC_THREAD
        fields += [(t + PE_MOUSE_X, x, 4), (t + PE_MOUSE_Y, y, 4), (t + PE_IDLE, idle, 4),
                   (t + PE_PAUSED, 0 if rng.random() < 0.9 else 1, 4),
                   (t + PE_CHAT_OPEN, 0 if rng.random() < 0.8 else 1, 1),
                   (t + 0x3c0, WORLD_BASE + PE_EVENTS, 4), (t + 0x3c4, WORLD_BASE + PE_TO_NET, 4)]
        fields += [(PE_EVENT_RING + 4 * i, d, 4) for i, d in enumerate(dwords)]
        fields += pe_queue(PE_EVENTS, PE_EVENT_RING, len(dwords)) + pe_queue(PE_TO_NET, PE_NET_RING, 0)
        fields += [(0x2d284, menu, 4), (MAP_SIZE, rng.choice([64, 128, 256, 384]), 4),
                   (LOCAL_PLAYER, rng.randrange(4), 4), (0x2d27c, 0, 4)]
        extra = PE_MENU_PRESETS.get(focus)
        if extra:
            extra(rng, fields)
        _pe_debug(focus, menu, (x, y), idle, events, dwords)
        return fields
    return presets


def pe_globals_for(focus: str):
    def globals_(rng, pool, args):
        mode, layout = _pe_case["layout"]
        fields = pe_layout_globals(layout, mode)
        fields += [(0x007DEB55, 0 if rng.random() < 0.85 else 1, 1),        # g_worldUpdateDisabled
                   (0x007A800A, 0 if rng.random() < 0.9 else 1, 1),         # g_optionRecord
                   (0x007A800B, 0 if rng.random() < 0.95 else 1, 1),        # g_optionPlay
                   (0x007DFDB0, rng.choice([0, 0, 0, 3, 0x44, 0x45, 99]), 4),  # g_campaignMission
                   (0x007DFD80, rng.randrange(2), 4),                       # g_scrollMode
                   (0x007ACF84, rng.choice([4, 8, 16, 40]), 4),             # g_scrollSpeed
                   (0x007DFDA8, 0 if rng.random() < 0.85 else 9, 4),        # g_cheatMode
                   (0x007DFDAC, rng.randrange(6), 4),                       # g_cheatKeys
                   (0x00730F64, rng.choice([0xFFFFFFFF, 0, 1, 2]), 4),      # g_gameOutcome
                   (PE_SET_EVENT_CALL, PE_SET_EVENT_STUB, 6)]
        extra = PE_MENU_GLOBALS.get(focus)
        if extra:
            fields += extra(rng)
        return fields
    return globals_


# ---------------------------------------------------------------- parts
# Each part of the function adds its presets/globals/callees here.
# --- part A (build menu, settler menus) ---
# (end of the part)
#
# --- part B (statistics) ---
# (end of the part)
#
# --- part C (building info) ---
# (end of the part)
#
# --- part D (message menus, players, soldiers) ---
# (end of the part)
#
# --- frame, keys, map ---
# (end of the part)
#

# The foci of the cases: 30 % general, the rest spread over the menus. For
# work on one part, the environment variable PE_FOCUS (e.g. "menu4") makes
# every case use that focus (development only; recorded passes need the
# default mixture).
PE_FOCI = ["menu1", "menu2", "menu3", "menu4", "menu5", "menu6", "menu7", "menu8"]


def pe_focus(rng) -> str:
    import os as _os
    forced = _os.environ.get("PE_FOCUS")
    if forced:
        return forced
    return "general" if rng.random() < 0.3 else rng.choice(PE_FOCI)


def pe_presets(rng, pool, args):
    focus = pe_focus(rng)
    _pe_case["focus"] = focus
    return pe_presets_for(focus)(rng, pool, args)


def pe_globals(rng, pool, args):
    return pe_globals_for(_pe_case["focus"])(rng, pool, args)


SPECS += [
    Spec("CLogicThread_ProcessEvents", 0x00524fa0, "logicthread", [], presets=pe_presets, globals=pe_globals,
         callees=PE_CALLEES, eax_mask=0, virtuals={0xc: 2},
         # The jump table 0x534c00 and its index 0x534cbc follow the last ret.
         cover_until=0x00534c00,
         case_scale=8),
]
