"""Emulator specs, part `graphics`: specs of the replacements in src/s3re/game/graphics/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def colour_table(rng, pool, args):
    """World +0xa0 points at a 257-slot uint16 table in scratch 0: slots
    1..n-1 filled (sometimes all), sometimes holding the colour asked for."""
    fields = [(0xa0, WORLD_BASE + SCRATCH, 4)]
    filled = 257 if rng.random() < 0.3 else rng.randrange(257)
    for slot in range(1, 257):
        value = rng.randrange(1, 0x10000) if slot < filled else 0
        fields.append((SCRATCH + slot * 2, value, 2))
    if rng.random() < 0.4:
        slot = rng.randrange(1, 257)
        fields.append((SCRATCH + slot * 2, args[0] & 0xFFFF, 2))
    return fields


def colour_arg(rng, pool):
    r = rng.random()
    if r < 0.05:
        return rng.getrandbits(16) << 16  # low word 0
    return rng.getrandbits(32)

SPECS += [
    Spec("Landscape_FindOrAddColour", 0x0054c940, "thiscall", [colour_arg], presets=colour_table,
         max_cases=500),
]

# -- batch 23: graphics helpers (graphics_helpers.cpp), MapEffect_SetLinks --

COLOUR_GLOBALS = 0x007dfba0


def colour_format(rng, pool, args):
    """Shifts (mostly 0..23) and scales (mostly 0..0x1ff) of the pixel format."""
    fields = []
    for i in range(3):
        fields.append((COLOUR_GLOBALS + 4 * i,
                       rng.randrange(24) if rng.random() < 0.9 else rng.getrandbits(32), 4))
        fields.append((COLOUR_GLOBALS + 12 + 4 * i,
                       rng.randrange(0x200) if rng.random() < 0.9 else rng.getrandbits(32), 4))
    return fields


def sprite_tables(rng, pool, args):
    """The group's table (scratch 0) and the entry it points to, placed so
    that entry + the group's offset lands in scratch 2."""
    group, index = args[0] >> 16, args[0] & 0xFFFF
    offset = rng.getrandbits(32)
    target = WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES + rng.randrange(0x800)
    return [(0xb24 + group * 4, WORLD_BASE + SCRATCH, 4),
            (0x264 + group * 4, offset, 4),
            (SCRATCH + 8 + index * 4, (target - offset) & 0xFFFFFFFF, 4)]

SPECS += [
    Spec("Render_PackColour", 0x004c7b00, "stdcall", [anything, anything, anything],
         globals=colour_format),
    Spec("CGraphicsData_SequenceFrameCount", 0x005fad20, "thiscall",
         [lambda rng, pool: (rng.randrange(0x100) << 16) | rng.randrange(0x100)],
         presets=sprite_tables),
    Spec("GfxDword_Construct", 0x005fc540, "thiscall", [anything]),
    Spec("Point16_Construct", 0x005fdab0, "thiscall", [anything, anything]),
    Spec("LogicRecord24_Construct", 0x005437b0, "thiscall", []),
]

SPECS += [
    Spec("DirectX_ThrowIfDDFailed", 0x00405d10, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS,
         new_fails=0.2),
    Spec("DirectX_ThrowIfDPFailed", 0x005bb890, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS,
         new_fails=0.2),
    surface_spec("DirectX_CSurface_CheckResult", 0x005bf2e0, [hresult, anything]),
    surface_spec("DirectX_CSurface_DDBlt", 0x005bf0c0,
                 [anything, one_of(0, SOURCE_SURFACE), anything, anything, anything]),
    surface_spec("DirectX_CSurface_DDBltFast", 0x005bf130,
                 [anything, anything, one_of(SOURCE_SURFACE), anything, anything]),
]

# -- batch 25: graphics and misc helpers ------------------------------------


def frame_offset_world(rng, pool, args):
    group = args[0] >> 16
    return [(0x37c + group * 4, WORLD_BASE + SCRATCH, 4), (SCRATCH + 8, rng.randrange(8), 2),
            (0xc40, rng.randrange(4), 4)]


def frame_id(rng, pool) -> int:
    return (rng.randrange(0xb0) << 16) | rng.randrange(8)


def put_pixel_world(rng, pool, args):
    return [(0x90, 2 * rng.randint(32, 64), 4), (0xa4, WORLD_BASE + SCRATCH, 4)]


def draw_origin(rng, pool, args):
    return [(0x00730f3c, rng.randrange(32), 4), (0x00730f40, rng.randrange(32), 4)]

SPECS += [
    Spec("CGraphicsData_TextAddress", 0x005faaa0, "thiscall", [frame_id, one_of(0, 1, 3, 0xFFFFFFFF)],
         presets=frame_offset_world),
    Spec("Render_PutPixel16", 0x004c7ac0, "thiscall", [below(64), below(64), anything],
         presets=put_pixel_world, globals=draw_origin),
    Spec("Render_Entry204", 0x004c7ab0, "thiscall", [below(256)],
         presets=lambda rng, pool, args: [(0x204, WORLD_BASE + SCRATCH, 4)]),
]

# -- batch 26: CSurface release and destructors (directx_surface.cpp) --------
#
# The surface at world offset 0 with its interface (+4) at SURFACE_IFACE
# (vtable = emu.FAKE_VTABLE, slot 8 = IUnknown::Release, a COM probe).


def surface_release_world(rng, pool, args):
    blocks = [(offset, rng.choice([0, WORLD_BASE + SCRATCH + offset]), 4) for offset in (0x94, 0x98, 0x9c, 0xa0, 0xa4)]
    return [(4, rng.choice([SURFACE_IFACE, SURFACE_IFACE, 0]), 4),
            (SURFACE_IFACE - WORLD_BASE, FAKE_VTABLE, 4)] + blocks


def surface_release_always(rng, pool, args):
    return [(4, SURFACE_IFACE, 4), (SURFACE_IFACE - WORLD_BASE, FAKE_VTABLE, 4)]


RELEASE_VIRTUALS = {0x08: (1, {}, {"com": True})}

SPECS += [
    Spec("DirectX_CSurface_Release", 0x005bf2c0, "thiscall", [], presets=surface_release_always,
         virtuals=RELEASE_VIRTUALS),
    Spec("DirectX_CSurface_Destroy", 0x005bf240, "thiscall", [], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("DirectX_CSurface_DeletingDestroy", 0x005bf220, "thiscall", [anything], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("CLandscapeSurface_Destroy", 0x0054be70, "thiscall", [], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
    Spec("CLandscapeSurface_DeletingDestroy", 0x0054be40, "thiscall", [anything], presets=surface_release_world,
         virtuals=RELEASE_VIRTUALS),
]


# -- session 54 (logic-misc): CDrawer draw queues, hit areas, pixel helpers
#    (drawer_queues.cpp), screen layout and ambient sounds (screen_layout.cpp).
#    ECX = the world buffer as the CDrawer object (graphics/drawer_layout.h).

DRAW_QUEUES = {"shadows": (0x20c, 0x14, 4000), "sprites": (0x14260, 0x1c, 8000),
               "colour_layers": (0x4b534, 0x14, 4000)}


def draw_queue(name):
    """The argument's row (when it indexes inside the drawer) ends at a valid
    record; the pool is sometimes (nearly) full."""
    base, size, capacity = DRAW_QUEUES[name]
    tails = base + size * capacity
    used = tails + 250 * 8

    def presets(rng, pool, args):
        row = args[0] - (1 << 32) if args[0] & 0x80000000 else args[0]
        fields = [(used, capacity - rng.randrange(3) if rng.random() < 0.25
                   else 250 + rng.randrange(capacity - 250), 4)]
        if -5 <= row < 260:
            fields.append((tails + row * 4, rng.randrange(capacity), 4))
        return fields
    return presets


def draw_row(rng, pool):
    return between(-5, 259)(rng, pool) if rng.random() < 0.15 else rng.randrange(250)


HIT_AREA_COUNT = 0x7dfd60
HIT_AREA_HALF = 0x7dfd64


def hit_area_globals(rng, pool, args):
    count = 10000 - rng.randrange(3) if rng.random() < 0.25 else rng.randrange(10000)
    return [(HIT_AREA_COUNT, count, 4), (HIT_AREA_HALF, rng.randrange(2), 4)]


DRAWER_PIXELS = 0x1000000  # world offset of the fake surface


def drawer_surface(rng, pool, args):
    pitch = rng.randrange(300) * 2 + (1 if rng.random() < 0.25 else 0)
    return [(0x90, pitch, 4), (0xa4, WORLD_BASE + DRAWER_PIXELS, 4), (0xec, WORLD_BASE + DRAWER_PIXELS, 4)]


def statistics_globals(rng, pool, args):
    return [(0x730f38, rng.randrange(6), 4), (0x730f3c, rng.randrange(16), 4), (0x730f40, rng.randrange(16), 4)]


def glyph_globals(rng, pool, args):
    row = args[1]
    return [(0x730f10, row + 1 if rng.random() < 0.35 else rng.randrange(400), 4)]


def glyph_bits(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.25 else rng.getrandbits(8)


def view_globals(rng, pool, args):
    return [(0x730f3c, rng.randrange(200) if rng.random() < 0.9 else rng.getrandbits(32), 4),
            (0x730f0c, 640 + rng.randrange(400) if rng.random() < 0.9 else rng.getrandbits(32), 4),
            (0x730f10, 480 + rng.randrange(300) if rng.random() < 0.9 else rng.getrandbits(32), 4)]


def view_x(rng, pool):
    return rng.choice([between(-40, 220), between(600, 1060), anything])(rng, pool)


def view_y(rng, pool):
    return rng.choice([between(-40, -20), between(470, 820), anything])(rng, pool)


def ambient_sources(rng, pool, args):
    """The kind's sources left and right (never both 0) and its count."""
    kind = args[0]
    left = rng.randrange(100000) if rng.random() < 0.25 else rng.randrange(40)
    right = (rng.randrange(100000) if rng.random() < 0.25 else rng.randrange(40)) + (1 if left == 0 else 0)
    return [(0x5f588 + kind * 4, left, 4), (0x5f5cc + kind * 4, right, 4),
            (0x5f610 + kind * 4, (rng.randrange(3000) - 100) & 0xFFFFFFFF, 4)]


SPECS += [
    Spec("CDrawer_QueueShadow", 0x004d7c90, "thiscall", [draw_row, anything, anything, anything],
         presets=draw_queue("shadows")),
    Spec("CDrawer_QueueShadowPart", 0x004d7d50, "thiscall", [draw_row] + [anything] * 4,
         presets=draw_queue("shadows")),
    Spec("CDrawer_QueueSprite", 0x004d7e10, "thiscall", [draw_row] + [anything] * 4,
         presets=draw_queue("sprites")),
    Spec("CDrawer_QueueSpritePart", 0x004d7f30, "thiscall", [draw_row] + [anything] * 5,
         presets=draw_queue("sprites")),
    Spec("CDrawer_QueueSpriteWithColour", 0x004d8050, "thiscall", [draw_row] + [anything] * 5,
         presets=draw_queue("sprites")),
    Spec("CDrawer_QueueColourLayer", 0x004d8170, "thiscall", [draw_row] + [anything] * 4,
         presets=draw_queue("colour_layers")),
    # EAX: a byte offset on one path, the caller's on the other (unused).
    Spec("CDrawer_AddHitArea", 0x004d8240, "stdcall", [anything] * 5, eax_mask=0, globals=hit_area_globals),
    Spec("CDrawer_DrawStatisticsBar", 0x004d7bd0, "thiscall", [below(12), below(12), below(12)],
         presets=drawer_surface, globals=statistics_globals),
    # EAX: the last pixel's index (callers don't use it).
    Spec("CDrawer_DrawGlyphRow", 0x004d6950, "thiscall", [below(30), below(200), glyph_bits], eax_mask=0,
         presets=drawer_surface, globals=glyph_globals),
    Spec("Render_SetScreenLayout", 0x004c6d70, "stdcall", [below(4, 0.2)]),
    Spec("Render_IsNearView", 0x004c7b50, "stdcall", [view_x, view_y], globals=view_globals),
    Spec("CDrawer_AmbientSoundPan", 0x004c79f0, "thiscall", [below(17)], presets=ambient_sources),
    Spec("CDrawer_AmbientSoundVolume", 0x004c7a50, "thiscall",
         [below(17), between(0, 1000), between(1001, 4000), between(-12000, 3000)], presets=ambient_sources),
]
