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

# -- Render::DrawValueBar (value_bar.cpp, session 55) ------------------------
# The drawing object of PutPixel16 (put_pixel_world: pitch, pixels in the
# scratch area), the drawing origin and a random pixel format; right is at
# least left + 2 (a width of 0 divides by zero, a negative one would draw
# 4 billion columns in both), top and bottom at least 1 (bottom 0 would run
# the inner rows to 0xffffffff).
def bar_limit(rng, pool):
    return rng.choice([0, 1, 2, rng.randrange(20), rng.randrange(5000), rng.getrandbits(17)])


def bar_value(rng, pool):
    return rng.choice([0, 1, 2, 3, rng.randrange(20), rng.randrange(5000), rng.getrandbits(17),
                       rng.getrandbits(32)])


def bar_globals(rng, pool, args):
    fields = [(0x00730f3c, rng.randrange(32), 4), (0x00730f40, rng.randrange(32), 4)]
    return fields + [(0x007dfba0 + 4 * i, rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(16), 4)
                     for i in range(6)]


SPECS += [
    Spec("Render_DrawValueBar", 0x004d79c0, "thiscall",
         [below(40), lambda rng, pool: 1 + rng.randrange(20), lambda rng, pool: 42 + rng.randrange(40),
          lambda rng, pool: 1 + rng.randrange(40), bar_value, bar_limit],
         eax_mask=0, presets=put_pixel_world, globals=bar_globals),
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
    # The configured mode (g_config +0x70) 0..3 or any.
    Spec("CDrawer_ApplyScreenLayout", 0x004c6d40, "thiscall", [],
         globals=lambda rng, pool, args: [(0x007a7af0 + 0x70, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]), 4)]),
    Spec("Render_IsNearView", 0x004c7b50, "stdcall", [view_x, view_y], globals=view_globals),
    Spec("CDrawer_AmbientSoundPan", 0x004c79f0, "thiscall", [below(17)], presets=ambient_sources),
    Spec("CDrawer_AmbientSoundVolume", 0x004c7a50, "thiscall",
         [below(17), between(0, 1000), between(1001, 4000), between(-12000, 3000)], presets=ambient_sources),
]

# -- session 55 (gfx): span functions (gfx_spans.cpp) -------------------------
#
# Fastcall spans: ECX = &dst (cell in scratch 0), EDX = dst end, then &src
# (cell +4) and the rest. The cells are set by the preset so that dst runs
# up to EDX in whole steps; pixels come from the lazily random world. The
# pixel-format masks are RGB565, RGB555 or random. EAX is not compared:
# the spans are only called through function pointers by the blitters,
# which don't read it (gfx_spans.cpp).

SHADE_MASKS = 0x007e8dd8
SPAN_CELLS = WORLD_BASE + SCRATCH
SPAN_DST = SCRATCH + 2 * SCRATCH_SLOT_BYTES
SPAN_SRC = SCRATCH + 4 * SCRATCH_SLOT_BYTES
SHADE_SPAN_ADDRESSES = (0x5f48e0, 0x5f4920, 0x5f4980, 0x5f4a00, 0x5f4a60, 0x5f4ae0, 0x5f4b60, 0x5f4c00,
                        0x5f4c60, 0x5f4ce0, 0x5f4d60, 0x5f4e00, 0x5f4e80, 0x5f4f20, 0x5f4fc0, 0x5f5070,
                        0x5f50d0, 0x5f5140, 0x5f51b0, 0x5f5250, 0x5f52c0, 0x5f5360, 0x5f5400, 0x5f54b0,
                        0x5f5520, 0x5f55c0, 0x5f5660, 0x5f5710, 0x5f57b0, 0x5f5860, 0x5f5910, 0x5f59e0)
MASKED_SPAN_ADDRESSES = (0x5fb470, 0x5fb410, 0x5fb3a0, 0x5fb340, 0x5fb2d0, 0x5fb260, 0x5fb1d0, 0x5fb180)


def shade_masks(rng, pool, args):
    pixel565 = (0x7bef, 0x39e7, 0x18e3, 0x861, 0x20)
    pixel555 = (0x3def, 0x1ce7, 0xc63, 0x421, 0)
    kind = rng.randrange(3)
    fields = []
    for k in range(5):
        if kind < 2:
            pixel = (pixel565, pixel555)[kind][k]
            pair = pixel << 16 | pixel
        else:
            pixel, pair = rng.getrandbits(32), rng.getrandbits(32)
        fields += [(SHADE_MASKS + 4 * k, pixel, 4), (SHADE_MASKS + 0x14 + 4 * k, pair, 4)]
    fields += [(SHADE_MASKS + 0x28, rng.getrandbits(32), 4), (SHADE_MASKS + 0x2c, rng.getrandbits(32), 4)]
    return fields


def span_end(rng, pool) -> int:
    return WORLD_BASE + SPAN_DST + 0x800 + 2 * rng.randrange(0x40)


def span_cells(step: int, max_units: int, past_end: float = 0.0):
    """dst = EDX - step * n (n 0..max_units-1, sometimes dst past the end),
    src (or the shadow phase) in the cell after it."""
    def presets(rng, pool, args):
        units = 0 if rng.random() < 0.1 else rng.randrange(max_units)
        dst = args[1] - step * units
        if rng.random() < past_end:
            dst = args[1] + 2 * rng.randrange(1, 8)
        return [(SCRATCH, dst, 4), (SCRATCH + 4, WORLD_BASE + SPAN_SRC + 2 * rng.randrange(0x80), 4)]
    return presets


def odd_flag(rng, pool) -> int:
    return rng.choice([0, 1, 0x100, 0xFFFFFF01, rng.getrandbits(32)])


def masked_cells(rng, pool, args):
    fields = span_cells(2, 120)(rng, pool, args)
    fields.append((SCRATCH + 8, rng.randrange(16) if rng.random() < 0.95 else rng.getrandbits(32) & 0xFF, 4))
    mask = SCRATCH + SCRATCH_SLOT_BYTES
    fields += [(mask + i, rng.choice([0, 1, 0, 1, rng.getrandbits(8)]), 1) for i in range(16)]
    return fields


def shadow_cells(rng, pool, args):
    fields = span_cells(2, 120, past_end=0.1)(rng, pool, args)
    return fields[:1] + [(SCRATCH + 4, rng.randrange(32) if rng.random() < 0.8 else rng.getrandbits(32), 4)]


SPECS += [
    Spec(f"ShadeSpan16_Level{level:02d}", address, "fastcall",
         [one_of(SPAN_CELLS), span_end, one_of(SPAN_CELLS + 4), odd_flag], eax_mask=0,
         presets=span_cells(4, 100), globals=shade_masks)
    for level, address in enumerate(SHADE_SPAN_ADDRESSES)
] + [
    Spec(f"MaskedSpan16_Level{level}", address, "fastcall",
         [one_of(SPAN_CELLS), span_end, one_of(SPAN_CELLS + 4), one_of(WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES),
          one_of(SPAN_CELLS + 8)], eax_mask=0, presets=masked_cells, globals=shade_masks)
    for level, address in enumerate(MASKED_SPAN_ADDRESSES)
] + [
    Spec("ShadowSpan_Dither", 0x005fd080, "fastcall",
         [one_of(SPAN_CELLS), span_end, one_of(SPAN_CELLS + 4), one_of(0, 1, 2, 0xFFFFFFFF)], eax_mask=0,
         presets=shadow_cells, globals=shade_masks),
    Spec("ShadowSpan_Halve", 0x005fd0c0, "fastcall",
         [one_of(SPAN_CELLS), span_end, one_of(SPAN_CELLS + 4), anything], eax_mask=0,
         presets=shadow_cells, globals=shade_masks),
    # Brightness above 255 reads past the tables (no range check), as the original.
    Spec("SelectBrightnessSpans", 0x005fb4c0, "fastcall",
         [lambda rng, pool: rng.randrange(256) if rng.random() < 0.95 else rng.randrange(0x800)], eax_mask=0),
    # Method 2.. asserts (Win32::ThrowAssertmentFailed throws; compared).
    Spec("SetShadowSpan", 0x005fcd30, "fastcall", [anything, anything, anything, anything, one_of(0, 1, 2, 0xFFFFFFFF)],
         eax_mask=0),
    Spec("CGraphicsData_SetShadowMethod", 0x005faa70, "thiscall", [one_of(0, 1, 1, 0, 7)], eax_mask=0,
         globals=shade_masks),
]

# -- session 55 (gfx): image and table accessors (gfx_images.cpp) -----------
#
# GfxDword methods: the reference at world 0 points into scratch slot 21
# (lazily random image bytes); out arguments point at scratch slot 20.
# CGraphicsData methods: gfx_world builds a graphics object at a world
# offset (0 for ECX = world, GFX_OTHER for SequenceFrame's argument): groups
# 0..3 with views = WORLD_BASE (offsets are world offsets) and all seven
# table kinds pointing at lists (slots 8..11, lazily random headers) or,
# for sequences, at frame records (slot 12).

IMAGE_REF = WORLD_BASE + SCRATCH + 21 * SCRATCH_SLOT_BYTES
OUT_SLOT = scratch(20)
GFX_OTHER = 0x10000
GFX_LISTS = SCRATCH + 8 * SCRATCH_SLOT_BYTES
GFX_RECORDS = SCRATCH + 12 * SCRATCH_SLOT_BYTES
GFX_TABLE_FIELDS = (0x494, 0x5ac, 0x6c4, 0x7dc, 0x8f4, 0xa0c, 0xb24)


def image_ref(null_chance: float = 0.0):
    def presets(rng, pool, args):
        image = 0 if rng.random() < null_chance else IMAGE_REF + rng.randrange(0x100)
        return [(0, image, 4)]
    return presets


def animation_id(rng, pool) -> int:
    if rng.random() < 0.25:
        return 0xFFFFFFFF
    return rng.randrange(4) << 16 | rng.randrange(72)


def gfx_world(base: int):
    def presets(rng, pool, args):
        fields = []
        for g in range(4):
            fields.append((base + 0x264 + 4 * g, WORLD_BASE, 4))
            for kind, field in enumerate(GFX_TABLE_FIELDS):
                table = SCRATCH + (1 + kind // 2) * SCRATCH_SLOT_BYTES + (kind % 2) * 0x800 + g * 0x200
                fields.append((base + field + 4 * g, WORLD_BASE + table, 4))
                fields.append((table + 6, rng.randrange(80), 2))
                for i in range(76):
                    if field == 0xb24:
                        entry = GFX_RECORDS + 24 * rng.randrange(80)
                    else:
                        entry = GFX_LISTS + 0x40 * rng.randrange(0x100)
                    fields.append((table + 8 + 4 * i, entry, 4))
        for r in range(100):
            record = GFX_RECORDS + 4 + 24 * r
            fields += [(record, rng.getrandbits(32), 4), (record + 4, animation_id(rng, pool), 4),
                       (record + 8, animation_id(rng, pool), 4), (record + 12, animation_id(rng, pool), 4),
                       (record + 16, rng.randrange(48), 2),
                       (record + 18, rng.randrange(48) if rng.random() < 0.8 else rng.getrandbits(16), 2),
                       (record + 20, rng.choice([0xFFFFFFFF, rng.getrandbits(32)]), 4)]
        return fields
    return presets


def gfx_id4(rng, pool) -> int:
    return rng.randrange(4) << 16 | rng.choice([0, 1, 2, rng.randrange(72)])


def list_index(rng, pool) -> int:
    return rng.choice([0, 1, rng.randrange(48), rng.randrange(300), 0xFFFFFFFF, rng.getrandbits(32) | 0x80000000])


def frame_fields(rng, pool, args):
    """A sequence frame at world 0 (sound -1 sometimes)."""
    return [(0x10, rng.choice([0xFFFFFFFF, rng.getrandbits(32)]), 4)]


def point_other(rng, pool, args):
    return [(SCRATCH + 22 * SCRATCH_SLOT_BYTES, rng.getrandbits(32), 4)]


# SequenceFrame::SequenceFrame: the three catch(...) blocks (log and
# rethrow) are only reachable through an exception; ours has none
# (gfx_images.cpp, known difference). 0x5fd902 is a false start inside it.
SEQUENCE_FRAME_CATCHES = (0x5fd8ba, 0x5fd8de, 0x5fd950)

SPECS += [
    Spec("GfxDword_PointAt4", 0x005f6430, "thiscall", [OUT_SLOT], presets=image_ref()),
    Spec("GfxDword_SpriteRleData16", 0x005f6440, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_IsValid", 0x005f6460, "thiscall", [], presets=image_ref(0.3)),
    Spec("GfxDword_PointAt0", 0x005f6470, "thiscall", [OUT_SLOT], presets=image_ref()),
    Spec("GfxDword_GuiExtraBytes", 0x005f6480, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_GuiExtraCount", 0x005f6490, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_GuiRleData", 0x005f64a0, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_TextureExtraBytes", 0x005fc550, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_TextureExtraCount", 0x005fc560, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_TextureExtraByte", 0x005fc570, "thiscall", [below(256)], presets=image_ref()),
    Spec("GfxDword_TextureRleData", 0x005fc580, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_ListImage", 0x005fc5a0, "thiscall", [OUT_SLOT, below(300)], presets=image_ref()),
    Spec("GfxDword_SpriteExtraBytes", 0x005fc5d0, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_SpriteExtraCount", 0x005fc5e0, "thiscall", [], presets=image_ref()),
    Spec("GfxDword_ListImageChecked", 0x005fcce0, "thiscall", [OUT_SLOT, list_index], presets=image_ref()),
    Spec("GfxDword_SpriteRleData", 0x005fcda0, "thiscall", [], presets=image_ref()),
    Spec("CGraphicsData_GetPixelFormat", 0x005fa730, "thiscall", [OUT_SLOT]),
    Spec("CGraphicsData_LanguageCount", 0x005faa50, "thiscall", []),
    Spec("CGraphicsData_SetLanguage", 0x005faa60, "thiscall", [anything]),
    Spec("CGraphicsData_ColorIndexedAnimationPointer", 0x005fab50, "thiscall", [OUT_SLOT, gfx_id4],
         presets=gfx_world(0)),
    Spec("CGraphicsData_ShadowAnimationPointer", 0x005fabb0, "thiscall", [OUT_SLOT, gfx_id4], presets=gfx_world(0)),
    Spec("CGraphicsData_PalettePointer", 0x005fac10, "thiscall", [gfx_id4, below(256, 0.1)], presets=gfx_world(0)),
    Spec("CGraphicsData_GuiImagePointerAt", 0x005faca0, "thiscall", [OUT_SLOT, gfx_id4, below(8)],
         presets=gfx_world(0)),
    Spec("CGraphicsData_LandscapeTexturePointer", 0x005face0, "thiscall", [OUT_SLOT, gfx_id4], presets=gfx_world(0)),
    Spec("CGraphicsData_GetSequenceFrame", 0x005fad50, "thiscall", [OUT_SLOT, gfx_id4, below(20)],
         presets=gfx_world(0), max_cases=500),
    Spec("SequenceFrame_SequenceFrame_default", 0x005fd7d0, "thiscall", []),
    Spec("SequenceFrame_SequenceFrame", 0x005fd800, "thiscall",
         [one_of(WORLD_BASE + GFX_OTHER), lambda rng, pool: WORLD_BASE + GFX_RECORDS + 4 + 24 * rng.randrange(99)],
         presets=gfx_world(GFX_OTHER), cover_until=0x005fd980, exempt_blocks=SEQUENCE_FRAME_CATCHES, max_cases=500),
    # AX only: the upper half of EAX is the caller's (every caller sign-extends AX).
    Spec("SequenceFrame_OffsetX", 0x005fd980, "thiscall", [], eax_mask=0xFFFF),
    Spec("SequenceFrame_OffsetY", 0x005fd990, "thiscall", [], eax_mask=0xFFFF),
    Spec("SequenceFrame_BodyImage", 0x005fd9a0, "thiscall", [OUT_SLOT]),
    Spec("SequenceFrame_ColorIndexedImage", 0x005fd9b0, "thiscall", [OUT_SLOT]),
    Spec("SequenceFrame_ShadowImage", 0x005fd9c0, "thiscall", [OUT_SLOT]),
    Spec("SequenceFrame_SoundId", 0x005fd9d0, "thiscall", [], presets=frame_fields),
    Spec("SequenceFrame_SoundHighByte", 0x005fd9f0, "thiscall", [], presets=frame_fields),
    Spec("Point16_Negate", 0x005fdad0, "thiscall", [OUT_SLOT]),
    # Ends with ret 8 at 0x5fdb25; 0x5fdb30 is the next (static initializer) code, no known start.
    Spec("Point16_Add", 0x005fdb00, "thiscall", [OUT_SLOT, scratch(22)], presets=point_other,
         cover_until=0x005fdb28),
]

# -- session 55 (gfx): RLE rows and the 16-bit blitters (gfx_rle.cpp) --------
#
# A blitter case is drawn as a whole by the first argument generator
# (sprite size, clip rectangle, a position classified the way
# GfxDword::DrawSprite 0x5fadb0 picks the blitter, construction progress,
# synthetic RLE rows); the other generators return its parts and the
# preset writes the RLE rows. The surface is lazily random world memory
# at BLIT_SURFACE; the spans are the original ones (globals).

BLIT_SURFACE = 0x100000
BLIT_RLE = 0x180000
BLIT_SURFACE_WIDTH = 160
BLIT_SURFACE_HEIGHT = 120
DESC_DWORDS = 27


def synthetic_rle(rng, width: int, height: int, pixel_bytes: int) -> bytes:
    out = bytearray()
    for _ in range(height):
        x = 0
        while True:
            skip = rng.randrange(width - x + 1) & 0x7F
            x += skip
            count = rng.randrange(width - x + 1) if x < width else 0
            x += count
            last = x >= width or rng.random() < 0.25
            out += bytes([count, skip | (0x80 if last else 0)])
            out += bytes(rng.getrandbits(8) for _ in range(count * pixel_bytes))
            if last:
                break
    return bytes(out)


def blit_case(kind: str):
    """Generators for (visible, hidden, desc[27], pos, size, rle, clip[4])
    and the preset writing the RLE rows; kind = the blitter."""
    state = {}

    def build(rng, pool):
        while True:
            width, height = rng.randint(1, 48), rng.randint(1, 24)
            left = rng.randrange(40)
            top = rng.randrange(30)
            right = left + 1 + rng.randrange(BLIT_SURFACE_WIDTH - left)
            bottom = top + 1 + rng.randrange(BLIT_SURFACE_HEIGHT - top)
            x = left - width + rng.randrange(right - left + width)
            y = top - height + rng.randrange(bottom - top + height)
            if x < 0 or x + width > BLIT_SURFACE_WIDTH or y < 0 or y + height > BLIT_SURFACE_HEIGHT:
                continue
            if rng.random() < 0.5:
                visible, hidden, h = height + 8, 0, height
            else:
                fraction = rng.randint(1, 0xFFFF)
                visible = ((height + 8) * fraction) >> 16
                kept = min(height, visible)
                hidden, h, y = height - kept, kept, y + height - kept
            if x < left:
                pick = "ClipLeft" if x + width < right else "ClipBoth"
            elif x + width >= right:
                pick = "ClipRight"
            else:
                pick = rng.choice(["Shaded", "FullBright"])
            if pick != kind:
                continue
            pitch = (BLIT_SURFACE_WIDTH + 2 * rng.randrange(3)) * 2
            desc = [rng.getrandbits(32) for _ in range(DESC_DWORDS)]
            desc[4] = pitch
            desc[9] = WORLD_BASE + BLIT_SURFACE
            state["rle"] = synthetic_rle(rng, width, height, 2)
            state["args"] = ([visible, hidden] + desc + [(x & 0xFFFF) | (y & 0xFFFF) << 16, width | h << 16,
                             WORLD_BASE + BLIT_RLE, left, top, right, bottom])
            return state["args"][0]

    def part(i):
        return lambda rng, pool: state["args"][i]

    def presets(rng, pool, args):
        return [(BLIT_RLE + i, b, 1) for i, b in enumerate(state["rle"])]

    return [build] + [part(i) for i in range(1, 2 + DESC_DWORDS + 7)], presets


def blit_globals(rng, pool, args):
    """RGB565 masks and a random pair of spans."""
    pixel565 = (0x7bef, 0x39e7, 0x18e3, 0x861, 0x20)
    fields = []
    for k in range(5):
        fields += [(SHADE_MASKS + 4 * k, pixel565[k], 4), (SHADE_MASKS + 0x14 + 4 * k, pixel565[k] * 0x10001, 4)]
    fields += [(0x007e8c74, rng.choice(SHADE_SPAN_ADDRESSES), 4), (0x007e8c70, rng.choice(MASKED_SPAN_ADDRESSES), 4)]
    return fields


for _kind, _address in (("ClipLeft", 0x005fb4f0), ("ClipBoth", 0x005fb850), ("ClipRight", 0x005fbdf0),
                        ("Shaded", 0x005fc160), ("FullBright", 0x005fc350)):
    _generators, _presets = blit_case(_kind)
    SPECS.append(Spec(f"BlitRle16_{_kind}", _address, "fastcall", _generators, eax_mask=0, presets=_presets,
                      globals=blit_globals, max_cases=1000))


def clip_cells(rng, pool) -> int:
    """One of four cells in scratch 0 (two arguments may alias)."""
    return WORLD_BASE + SCRATCH + 4 * rng.randrange(4)


def point_arg(rng, pool) -> int:
    return (rng.randrange(600) - 300) & 0xFFFF | ((rng.randrange(600) - 300) & 0xFFFF) << 16


def size_arg(rng, pool) -> int:
    return rng.getrandbits(16) | rng.randrange(120) << 16


def signed_small(n: int) -> Generator:
    return lambda rng, pool: (rng.randrange(2 * n) - n) & 0xFFFFFFFF


def skip_rows_case(pixel_bytes: int, two_counts: bool):
    """(desc*, rowsA, [rowsB,] &rle, &dst) with RLE rows at scratch slot 4."""
    state = {}

    def desc(rng, pool):
        state["rle"] = synthetic_rle(rng, rng.randint(1, 60), 40, pixel_bytes)
        return WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES

    def presets(rng, pool, args):
        fields = [(SCRATCH + 4 * SCRATCH_SLOT_BYTES + i, b, 1) for i, b in enumerate(state["rle"])]
        fields += [(SCRATCH, WORLD_BASE + SCRATCH + 4 * SCRATCH_SLOT_BYTES, 4),
                   (SCRATCH + SCRATCH_SLOT_BYTES + 0x10,
                    rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(4000), 4)]
        return fields

    if two_counts:
        counts = [below(20), below(20)]
    else:
        counts = [lambda rng, pool: (rng.randrange(20) - 3) & 0xFFFFFFFF]
    return [desc] + counts + [one_of(WORLD_BASE + SCRATCH), one_of(WORLD_BASE + SCRATCH + 4)], presets


def rle16_clip_args():
    """(visible, &skipTop, pos, size, clip[4], &rowsAfter, &masked,
    &maskStart); visible equals the height in about a third of the cases."""
    state = {}

    def first(rng, pool):
        state["size"] = size_arg(rng, pool)
        if rng.random() < 0.35:
            return state["size"] >> 16
        return (rng.randrange(140) - 4) & 0xFFFFFFFF

    return [first, clip_cells, point_arg, lambda rng, pool: state["size"], anything, signed_small(200), anything,
            signed_small(300), clip_cells, clip_cells, clip_cells]


_rle16_skip_args, _rle16_skip_presets = skip_rows_case(2, True)
_color_skip_args, _color_skip_presets = skip_rows_case(1, False)
_shadow_skip_args, _shadow_skip_presets = skip_rows_case(0, True)

SPECS += [
    Spec("Rle16VerticalClip", 0x005fb050, "fastcall", rle16_clip_args(), eax_mask=0),
    Spec("ColorIndexedVerticalClip", 0x005fc7d0, "fastcall",
         [clip_cells, clip_cells, point_arg, size_arg, anything, signed_small(200), anything, signed_small(300)],
         eax_mask=0),
    Spec("ShadowVerticalClip", 0x005fcff0, "fastcall",
         [clip_cells, clip_cells, point_arg, size_arg, anything, signed_small(200), anything, signed_small(300)],
         eax_mask=0),
    Spec("Rle16SkipRows", 0x005fb130, "fastcall", _rle16_skip_args, eax_mask=0, presets=_rle16_skip_presets),
    Spec("ColorIndexedSkipRows", 0x005fc810, "fastcall", _color_skip_args, eax_mask=0, presets=_color_skip_presets),
    Spec("ShadowSkipRows", 0x005fd040, "fastcall", _shadow_skip_args, eax_mask=0, presets=_shadow_skip_presets),
]

# -- session 55 (gfx): Draw methods, colour-indexed and shadow blitters ------
# (gfx_draw.cpp). Cases are built as a whole like blit_case: a synthetic
# sprite, a clip rectangle and a position whose classification picks the
# blitter under test (the Draw specs take any of them). Colour palettes
# are lazily random world memory; the shadow span is the original dither
# or halve span (0x7e8e20).

SHADOW_SPAN_GLOBAL = 0x007e8e20
DRAW_IMAGE = 0x180000      # world offset of the image (header + RLE rows)
DRAW_DESC = 0x1f0000       # the surface description
DRAW_POS = 0x1f1000
DRAW_CLIP = 0x1f1010
DRAW_PALETTE = 0x1f2000


def placement(rng, width: int, height: int):
    """A clip rectangle and a position overlapping it inside the surface."""
    while True:
        left = rng.randrange(40)
        top = rng.randrange(30)
        right = left + 1 + rng.randrange(BLIT_SURFACE_WIDTH - left)
        bottom = top + 1 + rng.randrange(BLIT_SURFACE_HEIGHT - top)
        x = left - width + rng.randrange(right - left + width)
        y = top - height + rng.randrange(bottom - top + height)
        if 0 <= x and x + width <= BLIT_SURFACE_WIDTH and 0 <= y and y + height <= BLIT_SURFACE_HEIGHT:
            return (left, top, right, bottom), x, y


def layer_side(x: int, width: int, clip) -> str:
    if x < clip[0]:
        return "ClipLeft"
    return "ClipRight" if x + width >= clip[2] else "Unclipped"


def random_desc(rng):
    desc = [rng.getrandbits(32) for _ in range(DESC_DWORDS)]
    desc[4] = (BLIT_SURFACE_WIDTH + 2 * rng.randrange(3)) * 2
    desc[9] = WORLD_BASE + BLIT_SURFACE
    return desc


def point(x: int, y: int) -> int:
    return (x & 0xFFFF) | (y & 0xFFFF) << 16


def layer_case(shadow: bool, side: str):
    state = {}

    def build(rng, pool):
        while True:
            width, height = rng.randint(1, 48), rng.randint(1, 24)
            clip, x, y = placement(rng, width, height)
            if layer_side(x, width, clip) != side:
                continue
            state["rle"] = synthetic_rle(rng, width, height, 0 if shadow else 1)
            if shadow:
                kept = height if rng.random() < 0.6 else rng.randint(0, height)
                hidden = height - kept
                origin = point(x + rng.randrange(80) - 40, y + hidden - rng.randrange(40))
                state["args"] = ([hidden, WORLD_BASE + BLIT_RLE] + random_desc(rng) +
                                 [point(x, y + hidden), width | kept << 16] + list(clip) + [origin])
            else:
                state["args"] = ([WORLD_BASE + BLIT_RLE, WORLD_BASE + DRAW_PALETTE] + random_desc(rng) +
                                 [point(x, y), width | height << 16] + list(clip))
            return state["args"][0]

    def part(i):
        return lambda rng, pool: state["args"][i]

    def presets(rng, pool, args):
        return [(BLIT_RLE + i, b, 1) for i, b in enumerate(state["rle"])]

    count = 2 + DESC_DWORDS + 2 + 4 + (1 if shadow else 0)
    return [build] + [part(i) for i in range(1, count)], presets


def shadow_globals(rng, pool, args):
    fields = blit_globals(rng, pool, args)
    fields.append((SHADOW_SPAN_GLOBAL, rng.choice([0x005fd080, 0x005fd0c0]), 4))
    return fields


for _side, _address in (("Unclipped", 0x005fc860), ("ClipRight", 0x005fc9a0), ("ClipLeft", 0x005fcb30)):
    _generators, _presets = layer_case(False, _side)
    SPECS.append(Spec(f"BlitColorIndexed_{_side}", _address, "fastcall", _generators, eax_mask=0, presets=_presets,
                      max_cases=1000))
# Dead blocks of the shadow blitters: the negative branches of C's % 32 on
# values that are never negative there: the second remainder after a
# negative origin coordinate was made positive (two per blitter), (origin y
# mod 32 + skipTop) (skipTop from ShadowVerticalClip is >= 0) and the row
# parity + 1.
SHADOW_DEAD_BLOCKS = {
    "Unclipped": (0x5fd1f6, 0x5fd218, 0x5fd233, 0x5fd2da),
    "ClipRight": (0x5fd3d8, 0x5fd3ec, 0x5fd409, 0x5fd52f),
    "ClipLeft": (0x5fd638, 0x5fd64c, 0x5fd669, 0x5fd78b),
}
for _side, _address in (("Unclipped", 0x005fd120), ("ClipRight", 0x005fd310), ("ClipLeft", 0x005fd570)):
    _generators, _presets = layer_case(True, _side)
    SPECS.append(Spec(f"BlitShadow_{_side}", _address, "fastcall", _generators, eax_mask=0, presets=_presets,
                      globals=shadow_globals, max_cases=1000, exempt_blocks=SHADOW_DEAD_BLOCKS[_side]))


def draw_case(kind: str):
    """thiscall: the GfxDword at world 0 points at an image at DRAW_IMAGE
    (sprite header + extra bytes + RLE rows, or a GUI header); desc, pos and
    clip in world memory (DrawGuiImage takes pos and clip by value)."""
    state = {}

    def build(rng, pool):
        width, height = rng.randint(1, 48), rng.randint(1, 24)
        clip, x, y = placement(rng, width, height)
        if rng.random() < 0.1:  # sometimes entirely outside the clip
            x = clip[2] + rng.randrange(1, 20) if clip[2] + 21 < BLIT_SURFACE_WIDTH else x
        offset_x, offset_y = rng.randrange(40) - 20, rng.randrange(40) - 20
        extra = rng.randrange(6)
        pixel_bytes = {"Sprite": 2, "GuiImage": 2, "ColorIndexed": 1, "Shadow": 0}[kind]
        rle = synthetic_rle(rng, width, height, pixel_bytes)
        if kind == "GuiImage":
            header = struct.pack("<HHhhB", width, height, offset_x, offset_y, extra)
        else:
            header = struct.pack("<IHHhhB", 0x0C, width, height, offset_x, offset_y, extra)
        image = header + bytes(rng.getrandbits(8) for _ in range(extra))
        if len(image) % 2:
            image += b"\x00"
        state["image"] = image + rle
        state["desc"] = random_desc(rng)
        state["clip"] = clip
        pos = point(x - offset_x, y - offset_y)
        state["pos"] = pos
        fraction = rng.choice([0, 0x10000, rng.randrange(0x10001), rng.randrange(0x10001)])
        brightness = rng.choice([248 + rng.randrange(8), rng.randrange(256), rng.randrange(256)])
        desc_ptr = WORLD_BASE + DRAW_DESC
        if kind == "Sprite":
            state["args"] = [desc_ptr, WORLD_BASE + DRAW_POS, WORLD_BASE + DRAW_CLIP, brightness, fraction]
        elif kind == "ColorIndexed":
            state["args"] = [desc_ptr, WORLD_BASE + DRAW_POS, WORLD_BASE + DRAW_CLIP, WORLD_BASE + DRAW_PALETTE]
        elif kind == "Shadow":
            state["args"] = [desc_ptr, WORLD_BASE + DRAW_POS, WORLD_BASE + DRAW_CLIP,
                             (rng.randrange(200) - 100) & 0xFFFFFFFF, fraction]
        else:
            state["args"] = [desc_ptr, pos] + list(clip) + [brightness]
        return state["args"][0]

    def part(i):
        return lambda rng, pool: state["args"][i]

    def presets(rng, pool, args):
        fields = [(0, WORLD_BASE + DRAW_IMAGE, 4)]
        fields += [(DRAW_IMAGE + i, b, 1) for i, b in enumerate(state["image"])]
        fields += [(DRAW_DESC + 4 * i, v, 4) for i, v in enumerate(state["desc"])]
        fields += [(DRAW_POS, state["pos"], 4)]
        fields += [(DRAW_CLIP + 4 * i, v, 4) for i, v in enumerate(state["clip"])]
        return fields

    count = 7 if kind == "GuiImage" else (4 if kind == "ColorIndexed" else 5)
    return [build] + [part(i) for i in range(1, count)], presets


# The GUI image draw's false starts 0x5f65f2 / 0x5f674b (DrawGuiImage_tail*)
# are inside it; the coverage runs to the next real function.
for _kind, _address, _extra in (("Sprite", 0x005fadb0, {}),
                                ("GuiImage", 0x005f64c0, {"cover_until": 0x005f67b0}),
                                ("ColorIndexed", 0x005fc5f0, {}), ("Shadow", 0x005fcdc0, {})):
    _generators, _presets = draw_case(_kind)
    SPECS.append(Spec(f"GfxDword_Draw{_kind}", _address, "thiscall", _generators, eax_mask=0, presets=_presets,
                      globals=shadow_globals, max_cases=1000, **_extra))

# -- session 55 (gfx): Close and IsPathOnRemovableDrive (gfx_files.cpp) ------
#
# Close: the gfx object at world 0 with views mapped or not (group 0
# mostly) and fake mapping objects (vtable emu.FAKE_VTABLE, slot 1 = +4
# probed). UnmapView 0x61aa40 is probed: it calls UnmapViewOfFile, which
# the emulator doesn't run. IsPathOnRemovableDrive: a std::string at
# scratch 0 with no buffer (the empty-string buffer 0x6553a0 is read) or a
# lazily random one; GetDriveTypeA is an API probe.

GFX_MAPPINGS = SCRATCH + 30 * SCRATCH_SLOT_BYTES


def close_world(rng, pool, args):
    fields = []
    for g in range(70):
        mapped = rng.random() < (0.8 if g == 0 else 0.5)
        fields.append((0x264 + 4 * g, rng.getrandbits(32) | 1 if mapped else 0, 4))
        fields.append((0x14c + 4 * g, WORLD_BASE + GFX_MAPPINGS + 0x40 * g, 4))
        fields.append((GFX_MAPPINGS + 0x40 * g, FAKE_VTABLE, 4))
    return fields


def removable_path(rng, pool, args):
    buffer = 0 if rng.random() < 0.25 else WORLD_BASE + SCRATCH + 0x100
    return [(SCRATCH + 4, buffer, 4)]


SPECS += [
    Spec("CGraphicsData_Close", 0x005fa690, "thiscall", [], eax_mask=0, presets=close_world,
         callees={0x0061aa40: (1, "thiscall", "probe")}, virtuals={0x04: 0}),
    Spec("IsPathOnRemovableDrive", 0x005fa750, "thiscall", [one_of(WORLD_BASE + SCRATCH), anything],
         presets=removable_path),
]


# -- session 55 round 2 (gfx-open): the loader (gfx_loader.cpp) ------------
#
# CGraphicsData at world 0. The Win32 calls of the original's callees are
# API probes (emu.API_HANDLERS); the throwers never return (callee kind
# "throw", see framework.py's WIN32_THROWERS). The name's allocator byte
# (+0x24) of the constructor and FindDirectoryContainingFile's result
# allocator byte come from the original's stack (see gfx_loader.cpp):
# ignored, with self-test pairings (self_test.py).

GFX_FOLDERS = SCRATCH + 12 * SCRATCH_SLOT_BYTES     # a WCValOrderedVector<string> (0x1c bytes)
GFX_FOLDER_DATA = SCRATCH + 13 * SCRATCH_SLOT_BYTES  # its strings (16 bytes each)
GFX_FOLDER_TEXT = SCRATCH + 14 * SCRATCH_SLOT_BYTES  # their texts (0x40 apart)
GFX_FILE = SCRATCH + 15 * SCRATCH_SLOT_BYTES        # the file name string, its text at +0x20
GFX_RESULT = SCRATCH + 16 * SCRATCH_SLOT_BYTES      # the result string
GFX_FORMAT = SCRATCH + 17 * SCRATCH_SLOT_BYTES      # a DDPIXELFORMAT
GFX_BASE = SCRATCH + 18 * SCRATCH_SLOT_BYTES        # the base path string, its text at +0x20
GFX_FAKES = SCRATCH + 19 * SCRATCH_SLOT_BYTES       # fake mapping objects (0x20 apart)
GFX_FAKE_HEADERS = SCRATCH + 22 * SCRATCH_SLOT_BYTES     # fake GFX file headers (0x100 apart)
GFX_FOUND = SCRATCH + 26 * SCRATCH_SLOT_BYTES       # FindDirectory's result buffer (Open)


def gfx_string(guest, address):
    """A std::string's length and text (its buffer differs between frames)."""
    ptr, length = struct.unpack("<II", guest(address + 4, 8))
    text = guest(ptr, min(length, 0x100)) if ptr else b""
    return (length, text)


def gfx_message_record(machine, ecx, args):
    """A thrower's record without the message's allocator byte (the
    original's comes from an uninitialized local for some calls)."""
    from unicorn.x86_const import UC_X86_REG_EDX
    message = machine.guest_read(args[-1], 16)[1:] if args[-1] else None
    return (ecx, machine.uc.reg_read(UC_X86_REG_EDX), *args[:-1], message)


GFX_THROWERS = {
    0x00610de0: (3, "thiscall", "throw", None, gfx_message_record),   # ThrowWin32Error
    0x00611050: (3, "thiscall", "throw", None, thrower_record),       # ThrowHResult
    0x00611db0: (2, "thiscall", "this", None, file_not_found_record),  # CFileNotFound(name, 1)
    0x00610960: (1, "thiscall", "throw"),                              # ThrowOutOfMemory(text, file, line)
    0x006119b0: (1, "thiscall", "throw"),                              # ThrowAssertmentFailed(text, file, line)
}


def gfx_text_string(rng, offset, texts):
    """A string at world `offset` holding one of `texts` (or nothing), its
    buffer at offset + 0x20."""
    text = rng.choice(texts)
    if text is None:
        return [(offset, 0, 4), (offset + 4, 0, 4), (offset + 8, 0, 4), (offset + 12, 0, 4)]
    fields = [(offset, rng.getrandbits(8), 1), (offset + 4, WORLD_BASE + offset + 0x21, 4),
              (offset + 8, len(text), 4), (offset + 12, len(text) + rng.randrange(4), 4),
              (offset + 0x20, rng.choice([0, 0, 1]), 1)]
    fields += [(offset + 0x21 + i, b, 1) for i, b in enumerate(text + b"\0")]
    return fields


def gfx_folders(rng, pool, args):
    """0..6 folder strings in a WCValOrderedVector<string> (real vtable
    0x655440: an empty one grows in the index check), and the file name."""
    count = rng.choice([0, 1, 2, 3, 5, 6])
    capacity = rng.choice([count, count + 2, 10]) if count else rng.choice([0, 0, 4])
    fields = [(GFX_FOLDERS, 0x655440, 4), (GFX_FOLDERS + 4, 0, 4),
              (GFX_FOLDERS + 8, WORLD_BASE + GFX_FOLDER_DATA if capacity else 0, 4),
              (GFX_FOLDERS + 12, capacity, 4), (GFX_FOLDERS + 16, count, 4), (GFX_FOLDERS + 20, count, 4),
              (GFX_FOLDERS + 24, rng.choice([5, 5, 0]), 4)]
    for i in range(count):
        text = rng.choice([b".", b"Gfx", b".\\Gfx", b"", b"..", b"D:\\S3\\Gfx"])
        buffer = GFX_FOLDER_TEXT + 0x40 * i
        if text:
            fields += [(GFX_FOLDER_DATA + 16 * i + 4, WORLD_BASE + buffer + 1, 4), (buffer, 0, 1)]
            fields += [(buffer + 1 + j, b, 1) for j, b in enumerate(text + b"\0")]
        else:
            fields.append((GFX_FOLDER_DATA + 16 * i + 4, 0, 4))
        fields += [(GFX_FOLDER_DATA + 16 * i + 8, len(text), 4), (GFX_FOLDER_DATA + 16 * i + 12, len(text), 4)]
    fields += gfx_text_string(rng, GFX_FILE, [b"siedler3_00.f8007e01f.dat", b"x.dat", None])
    fields += [(GFX_RESULT + i, 0, 4) for i in range(0, 16, 4)]
    return fields


def gfx_lifetime(rng, pool, args):
    """Mappings (fake objects, slot 0 probed) or none, views, a name."""
    fields = gfx_text_string(rng, 0x24, [b"C:\\Games\\S3\\Gfx\\", b"", None])
    for g in range(70):
        mapping = 0 if rng.random() < 0.1 else WORLD_BASE + GFX_FAKES + 0x20 * g
        fields += [(0x14c + 4 * g, mapping, 4), (GFX_FAKES + 0x20 * g, FAKE_VTABLE, 4)]
    fields.append((0x264, rng.choice([0, rng.getrandbits(32)]), 4))
    return fields


def detach_probe_record(machine, ecx, args):
    from unicorn.x86_const import UC_X86_REG_EDX
    return (ecx, machine.uc.reg_read(UC_X86_REG_EDX))


def detach_result(h):
    return 0 if h & 3 else 0x80000000 | (h >> 8) & 0xFFFF


def gfx_detach_world(rng, pool, args):
    fields = gfx_text_string(rng, 0x24, [b"C:\\S3\\Gfx\\", b"D:\\Gfx\\", b"A:", None])
    for g in range(70):
        fields += [(0x264 + 4 * g, rng.getrandbits(32) | 1 if rng.random() < 0.4 else 0, 4),
                   (0x34 + 4 * g, rng.getrandbits(32), 4)]
    return fields


def find_directory_record(machine, ecx, args):
    """FindDirectoryContainingFile(result, folders, file): the folders'
    texts and the file name (all on the caller's stack)."""
    guest = machine.guest_read
    length = struct.unpack("<I", guest(args[1] + 0x14, 4))[0]
    data = struct.unpack("<I", guest(args[1] + 8, 4))[0]
    folders = tuple(gfx_string(guest, data + 16 * i) for i in range(min(length, 8)))
    return (ecx, length, folders, gfx_string(guest, args[2]))


def find_directory_effect(machine, ecx, args, h):
    """The result: "C:\\S3\\Gfx\\" in a buffer of the world (count byte 0)."""
    text = b"C:\\S3\\Gfx\\"
    machine.guest_write(WORLD_BASE + GFX_FOUND, b"\0" + text + b"\0")
    machine.guest_write(args[0], struct.pack("<BxxxIII", 0, WORLD_BASE + GFX_FOUND + 1, len(text), len(text)))
    return args[0]


def text_record(machine, ecx, args):
    """CFile::Open: the path's text and the other arguments."""
    text = machine.guest_read(args[0], 0x80).split(b"\0")[0] if args[0] else None
    return (ecx, text, *args[1:])


def map_view_effect(machine, ecx, args, h):
    """MapView: one of four fake file headers (sometimes the wrong magic)."""
    return WORLD_BASE + GFX_FAKE_HEADERS + 0x100 * (h & 3)


def gfx_open_world(rng, pool, args):
    """Mapping objects (fake, their calls probed), a pixel format (RGB565,
    RGB555 or another), a base path, four file headers with random table
    offsets (text tables with 0..8 languages); the name (+0x24) empty or
    holding a folder."""
    fields = gfx_text_string(rng, 0x24, [None, b"C:\\old\\"])
    for g in range(70):
        fields.append((0x14c + 4 * g, WORLD_BASE + GFX_FAKES + 0x20 * g, 4))
    masks = rng.choice([(0xF800, 0x7E0, 0x1F), (0xF800, 0x7E0, 0x1F), (0x7C00, 0x3E0, 0x1F),
                        (0xF800, 0x3E0, 0x1F), (0x7C00, 0x3E0, 0x3F), (0xFF0000, 0xFF00, 0xFF)])
    fields += [(GFX_FORMAT + 4 * i, v, 4) for i, v in enumerate((0x20, 0x40, 0, 16) + masks + (0,))]
    fields += gfx_text_string(rng, GFX_BASE, [b"", b"C:\\S3\\", None, b"..\\"])
    for k in range(4):
        header = GFX_FAKE_HEADERS + 0x100 * k
        magic = 0x1404 if rng.random() < 0.03 else 0x41304
        fields.append((header, magic, 4))
        for t in range(8):
            fields.append((header + 0x34 + 4 * t, 0x60 + 0x10 * t if rng.random() < 0.7 else rng.randrange(0x400), 4))
        fields.append((header + 0x60 + 0x0a, rng.choice([0, 0, 8, 1, rng.getrandbits(16)]), 2))
    fields.append((0xC3C, rng.choice([0, 0, 3]), 4))
    return fields


def gfx_open_globals(rng, pool, args):
    return [(0x007A7AF0 + 0x159, rng.choice([0, 1, 1]), 1)]  # g_config.gfx_group61


# Never reached (exempt below). FindDirectoryContainingFile: the message
# clean-ups after its throwers (0x5f6b08, 0x5f6bfd, 0x5f6d68, 0x5f6eb1),
# the catch block (0x5f6c27..0x5f6c3a, only reached by a caught C++
# exception: the in-process test GfxLoader_MatchOriginal covers it with
# scripted SetCurrentDirectoryA failures) and the null check of a stack
# buffer (0x5f6dda). DetachFromMedium: everything after ThrowHResult and the
# frees of the message's "" temporaries. Destroy: the inlined string
# destructor's _Grow(0) success (0x5f69af; it never succeeds for 0) and the
# shared-count decrement after the split (0x5f69cf, 0x5f69d3; the split
# leaves an unshared buffer). Open: the null-path fallback of CFile::Open's
# name (0x5f9e93; the path is never empty).
CLEANUP_FIND_DIRECTORY = (0x5f6b08, 0x5f6bfd, 0x5f6c27, 0x5f6c2e, 0x5f6c34, 0x5f6c3a, 0x5f6d68, 0x5f6dda,
                          0x5f6eb1)
CLEANUP_GFX_DETACH = (0x5fa8f0, 0x5fa8fa, 0x5fa8fe, 0x5fa904, 0x5fa95c, 0x5fa973, 0x5fa97a, 0x5fa97e, 0x5fa9a8,
                      0x5fa9ac, 0x5fa9ae, 0x5fa9b2, 0x5fa9d7, 0x5fa9e2, 0x5fa9e6, 0x5fa9f2, 0x5fa9fc, 0x5faa00,
                      0x5faa06, 0x5faa0f)
CLEANUP_GFX_DESTROY = (0x5f69af, 0x5f69cf, 0x5f69d3)

GFX_OPEN_CALLEES = {
    **GFX_THROWERS,
    0x005f6a10: (3, "thiscall", "probe", find_directory_effect, find_directory_record),  # FindDirectoryContainingFile
    0x00619610: (7, "thiscall", "probe", None, text_record),                             # CFile::Open
    0x0061a080: (1, "thiscall", "probe"),                                                # CFile::GetSize
    0x0061a520: (6, "thiscall", "probe"),                                                # CFileMapping::Create
    0x0061a7d0: (4, "thiscall", "probe", map_view_effect),                               # CFileMapping::MapView
}

SPECS += [
    Spec("CGraphicsData_Construct", 0x005f67c0, "thiscall", [], new_fails=0.05, new_fails_size=0x2c,
         ignore_bytes=(0x24,)),
    Spec("CGraphicsData_Destroy", 0x005f68b0, "thiscall", [], eax_mask=0, presets=gfx_lifetime,
         virtuals={0x00: 1}, callees={0x005fa690: (0, "thiscall", "probe")},   # Close (verified, own spec)
         exempt_blocks=CLEANUP_GFX_DESTROY),
    Spec("CGraphicsData_DeletingDestroy", 0x005f6890, "thiscall", [anything], presets=gfx_lifetime,
         virtuals={0x00: 1}, callees={0x005fa690: (0, "thiscall", "probe")}),
    Spec("CGraphicsData_FindDirectoryContainingFile", 0x005f6a10, "thiscall",
         [one_of(WORLD_BASE + GFX_RESULT), one_of(WORLD_BASE + GFX_FOLDERS), one_of(WORLD_BASE + GFX_FILE)],
         presets=gfx_folders, callees=GFX_THROWERS, ignore_bytes=(GFX_RESULT,), cover_until=0x005f6fc5,
         new_fails=0.05, new_fails_size=0x28,
         exempt_blocks=CLEANUP_FIND_DIRECTORY),
    Spec("CGraphicsData_DetachFromMedium", 0x005fa7c0, "thiscall", [anything], presets=gfx_detach_world,
         callees={**GFX_THROWERS, 0x0061ab60: (0, "thiscall", detach_result, None, detach_probe_record)},
         exempt_blocks=CLEANUP_GFX_DETACH),
    Spec("CGraphicsData_Open", 0x005f6fd0, "thiscall",
         [one_of(WORLD_BASE + GFX_FORMAT), one_of(WORLD_BASE + GFX_BASE), one_of(0, 1, 2, 2, 3, 3, 7),
          one_of(0, 1, 2, 3, 4, 4, 9)],
         eax_mask=0, presets=gfx_open_world, globals=gfx_open_globals, callees=GFX_OPEN_CALLEES,
         new_fails=0.03, new_fails_size=0x0c, max_cases=400, exempt_blocks=(0x5f9e93,)),
]


# -- CDrawer::QueueCellObject (drawer_objects.cpp, session 57) ---------------
# The drawer at world offset 0 (only its +0x5f6b8 is read and passed on), the
# draw data at QCO_DATA (lazily random cells); the callees are probed: the
# getters write into their out argument (the animation pointer: a sequence
# record at QCO_SEQUENCE, lazily random) and return it, Entry204 returns
# 0..3 (an index into the animation table).
QCO_DATA = 0x100000
QCO_SEQUENCE = SCRATCH


def qco_out(value):
    def effect(m, ecx, args, h):
        m.guest_write(args[0], struct.pack("<I", value(h, args)))
        return args[0]
    return effect


def qco_this_value(m):
    from unicorn.x86_const import UC_X86_REG_ECX
    return struct.unpack("<I", bytes(m.guest_read(m.uc.reg_read(UC_X86_REG_ECX), 4)))[0]


QCO_CALLEES = {
    0x004c7ab0: (1, "thiscall", lambda h: h & 3),  # Render::Entry204
    0x005fabb0: (2, "thiscall", "probe", qco_out(lambda h, a: h & 0xFFFFFFFF), lambda m, ecx, a: (a[1],)),
    # (`this` is a stack local: "stdcall" leaves ECX out, its value is recorded)
    0x005fcce0: (2, "stdcall", "probe", qco_out(lambda h, a: (h >> 8) & 0xFFFFFFFF),
                 lambda m, ecx, a: (qco_this_value(m), a[1])),
    0x005faaf0: (2, "thiscall", "probe", qco_out(lambda h, a: WORLD_BASE + QCO_SEQUENCE), lambda m, ecx, a: (a[1],)),
    0x004d7c90: (4, "thiscall", "probe"),  # CDrawer::QueueShadow
    0x004d7e10: (5, "thiscall", "probe"),  # CDrawer::QueueSprite
}


def qco_sequence(rng, pool, args):
    return [(QCO_SEQUENCE + 7, rng.choice([0, 1, 2, 5, rng.randrange(256)]), 1)]


SPECS += [
    Spec("CDrawer_QueueCellObject", 0x004d5a50, "thiscall",
         [one_of(WORLD_BASE + QCO_DATA), below(130), below(130), anything, anything],
         presets=qco_sequence, callees=QCO_CALLEES, eax_mask=0, cover_until=0x004d5bd0),
]


# -- wip/logic-r4: CDrawer::SetDisplayMode / ChangeDisplayMode (drawer_display_mode.cpp)
# The drawer (ECX = the world base) has FAKE_VTABLE (slots 5 and 11 probed)
# and random surfaces flag +0x7c; DDGetDisplayMode is probed and writes a
# mode into the local DDSURFACEDESC (mostly one the game uses; the
# recorded argument, a frame address, is left out); RequestFullRedraw is
# probed, g_drawThread (0x7e8bec) null or not; SendMessageA is an API probe.
DISPLAY_MODES = ((640, 480, 16), (800, 600, 16), (1024, 768, 16))


def display_mode_desc(machine, ecx, args, h):
    width, height, bits = DISPLAY_MODES[h % 3]
    if (h >> 8) % 8 == 0:
        width = (h >> 12) % 2000
    if (h >> 24) % 8 == 0:
        height = (h >> 28) % 2000
    if (h >> 40) % 8 == 0:
        bits = (h >> 44) % 33
    refresh = 0 if (h >> 50) & 1 else 60 + 10 * ((h >> 52) % 3)
    machine.guest_write(args[0] + 8, struct.pack("<II", height, width))
    machine.guest_write(args[0] + 0x18, struct.pack("<I", refresh))
    machine.guest_write(args[0] + 0x54, struct.pack("<I", bits))


DISPLAY_MODE_CALLEES = {
    0x005bed70: (1, "thiscall", "probe", display_mode_desc, lambda machine, ecx, args: ()),  # DDGetDisplayMode
    0x005b6730: (0, "thiscall", "probe"),  # CGameDrawThread::RequestFullRedraw
}


def display_mode_drawer(rng, pool, args):
    return [(0, FAKE_VTABLE, 4), (0x7c, rng.choice([0, 1, rng.getrandbits(8)]), 1)]


def display_mode_globals(rng, pool, args):
    return [(0x007e8bec, rng.choice([0, 0x10000000 + rng.getrandbits(20) * 4]), 4)]


def display_dimension(which):
    def generate(rng, pool):
        return rng.getrandbits(11) if rng.random() < 0.15 else rng.choice(DISPLAY_MODES)[which]
    return generate


def change_display_options(rng, pool, args):
    mode = rng.choice([0, 1, 2, 3, 4, 250, rng.getrandbits(32)])
    return display_mode_drawer(rng, pool, args) + [(SCRATCH + 0xa0, mode, 4)]


def change_display_globals(rng, pool, args):
    return display_mode_globals(rng, pool, args) + [
        (0x00730f08, rng.choice([0, 1, 2, 3, 4]), 4), (0x007dfd30, rng.choice([0, 0, 1]), 4)]


SPECS += [
    Spec("CDrawer_SetDisplayMode", 0x0042ce90, "thiscall",
         [display_dimension(0), display_dimension(1), lambda rng, pool: rng.choice([16, 16, 8, rng.getrandbits(6)]),
          lambda rng, pool: rng.choice([0, 0, 60, 70, 80]), anything],
         presets=display_mode_drawer, globals=display_mode_globals, callees=DISPLAY_MODE_CALLEES,
         virtuals={0x14: 1, 0x2c: 1}),
    # EAX: AL only (the caller tests it).
    Spec("CDrawer_ChangeDisplayMode", 0x004c7490, "thiscall", [lambda rng, pool: WORLD_BASE + SCRATCH],
         presets=change_display_options, globals=change_display_globals, callees=DISPLAY_MODE_CALLEES,
         virtuals={0x14: 1, 0x2c: 1}, eax_mask=0xFF),
]


# -- wip/logic-r4: CLandscapeSurface::CLandscapeSurface (directx_surface.cpp) --
# The base constructor runs (verified); operator new fails for one of the
# four table sizes in some cases; ThrowOutOfMemory ends the run as a throw.
SPECS += [
    Spec("CLandscapeSurface_Construct", 0x0054bd30, "thiscall", [anything],
         callees={0x00610960: (1, "thiscall", "throw")}, new_fails=0.4,
         new_fails_size=(0x364, 1000000, 0x202, 0x10000)),
]
