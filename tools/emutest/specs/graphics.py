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
    Spec("GfxDword_ListImageChecked_4b19c0", 0x004b19c0, "thiscall", [OUT_SLOT, list_index], presets=image_ref()),
    Spec("GfxDword_Construct_609900", 0x00609900, "thiscall", [anything]),
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


# -- wip/logic-r4: CDrawer::PlayAmbientSounds (screen_layout.cpp) -------------
# The drawer at the world base: source counts around the seven sounds'
# minimums, left/right counts not both 0 (the pan divides by their sum);
# the draw data in the scratch area (ticks -1 or not, 0..10 queued
# effects). IsEnabled, GetEffect (returns a fake effect) and Play are probed,
# the CRT's rand probed (0..0x7fff); Pan and Volume run (verified).
AMBIENT_COUNTS = (0, 199, 200, 201, 400, 401, 800, 801, 2000, 2001, 8000, 8001, 30000)


def ambient_play_state(rng, pool, args):
    fields = [(0x5f6bc, 0x10000000 + rng.getrandbits(16) * 4, 4)]
    for k in range(17):
        left = rng.randrange(1, 40)
        fields += [(0x5f610 + 4 * k, rng.choice(AMBIENT_COUNTS), 4), (0x5f588 + 4 * k, left, 4),
                   (0x5f5cc + 4 * k, rng.randrange(0, 40), 4)]
    data = SCRATCH
    fields += [(data + 0x5c, rng.choice([0xFFFFFFFF, 0, 1, 5]), 4),
               (data + 0x260, rng.choice([0, 0, 1, 3, rng.randrange(11)]), 4)]
    return fields


AMBIENT_PLAY_CALLEES = {
    0x005ff4d0: (0, "thiscall", lambda h: 1 if h % 5 else 0),             # CSoundData::IsEnabled
    0x005ff4f0: (5, "thiscall", lambda h: 0x20000000 + (h & 0xFFF0)),     # CSoundData::GetEffect
    0x005c2fc0: (0, "thiscall", "probe"),                                 # CSoundEffect::Play
    0x0061f802: (0, "cdecl", lambda h: (h >> 8) & 0x7FFF if h & 3 else 29000 + (h >> 8) % 3800),  # rand
}

SPECS += [
    Spec("CDrawer_PlayAmbientSounds", 0x004c7730, "thiscall", [lambda rng, pool: WORLD_BASE + SCRATCH],
         eax_mask=0, presets=ambient_play_state, callees=AMBIENT_PLAY_CALLEES),
]


# -- CDrawer::QueueLandscapeObject (drawer_landscape.cpp, session 59) --------
# The drawer at world offset 0 (its +0x5f6b8 graphics, +0x5f6bc sound data
# and ambient count 16 are lazily random), the draw data at QLO_DATA; the
# presets give the cell of (column, row) and the draw data's mode fields
# values the branches test. The callees are probed like QueueCellObject's
# (the getters write into their out argument: the animation pointer a
# sequence record at QLO_SEQUENCE, lazily random but with a small frame
# count often); CDrawer::AddHitArea runs (it writes S3.EXE globals, compared).
QLO_DATA = 0x100000
QLO_SEQUENCE = SCRATCH
QLO_CELLS = QLO_DATA + 0x500

QLO_CALLEES = {
    0x004c7ab0: (1, "thiscall", lambda h: h & 3),  # Render::Entry204
    0x005fabb0: (2, "thiscall", "probe", qco_out(lambda h, a: h & 0xFFFFFFFF), lambda m, ecx, a: (a[1],)),
    0x005fab50: (2, "thiscall", "probe", qco_out(lambda h, a: (h >> 4) & 0xFFFFFFFF), lambda m, ecx, a: (a[1],)),
    0x005faaf0: (2, "thiscall", "probe", qco_out(lambda h, a: WORLD_BASE + QLO_SEQUENCE), lambda m, ecx, a: (a[1],)),
    0x005fac10: (2, "thiscall", "probe"),  # CGraphicsData::PalettePointer
    # (`this` is a stack local: "stdcall" leaves ECX out, its value is recorded)
    0x005fcce0: (2, "stdcall", "probe", qco_out(lambda h, a: (h >> 8) & 0xFFFFFFFF),
                 lambda m, ecx, a: (qco_this_value(m), a[1])),
    0x005fc5a0: (2, "stdcall", "probe", qco_out(lambda h, a: (h >> 12) & 0xFFFFFFFF),
                 lambda m, ecx, a: (qco_this_value(m), a[1])),
    0x004d7c90: (4, "thiscall", "probe"),  # CDrawer::QueueShadow
    0x004d7e10: (5, "thiscall", "probe"),  # CDrawer::QueueSprite
    0x004d8170: (5, "thiscall", "probe"),  # CDrawer::QueueColourLayer
    0x004d8050: (6, "thiscall", "probe"),  # CDrawer::QueueSpriteWithColour
    0x004c7b50: (2, "thiscall", "probe"),  # Render::IsNearView
    0x004c7a50: (4, "thiscall", "probe"),  # CDrawer::AmbientSoundVolume
    0x004c79f0: (1, "thiscall", "probe"),  # CDrawer::AmbientSoundPan
    0x005ff4d0: (0, "thiscall", "probe"),  # CSoundData::IsEnabled
    0x005ff4f0: (2, "thiscall", "probe"),  # CSoundData::GetEffect
    0x005c2fc0: (3, "thiscall", "probe"),  # DirectX::CSoundEffect::Play
    0x0061f802: (0, "cdecl", lambda h: h & 0x7FFF),  # rand
}

# Objects: every branch's values and their edges, sometimes any byte or dword.
QLO_OBJECTS = (0, 0, 0, 0xff, 0xfe, 0x44, 0x4a, 0x51, 0x54) + tuple(range(0x55, 0x70)) + (0x70, 0x71, 0x72, 0x73, 0x7f, 0x80, 0x87, 0xb7, 0xb8, 0xcf, 0xd0, 0xe7) + tuple(range(0xe8, 0xfe)) \
    + (1, 12, 13, 16, 17, 0x14, 0x15, 0x1c, 0x1d, 0x22, 0x23, 0x43, 0x52, 0x53)


def qlo_object(rng, pool):
    r = rng.random()
    if r < 0.8:
        return rng.choice(QLO_OBJECTS)
    if r < 0.95:
        return rng.randrange(256)
    return anything(rng, pool)


def qlo_presets(rng, pool, args):
    cell = QLO_CELLS + (args[1] * 130 + args[2]) * 0x30
    fields = [(QLO_SEQUENCE + 7, rng.choice([0, 1, 2, 5, 20, rng.randrange(256)]), 1),
              (QLO_DATA + 0x60, rng.choice([1, 1, 4, 4, 0, rng.getrandbits(32)]), 4),
              (QLO_DATA + 0x64, rng.choice([0, 1, 2, 3, 4, 5]), 4),
              (QLO_DATA + 0x68, rng.choice([rng.randrange(14), rng.randrange(6, 13)]), 4),
              (QLO_DATA + 0x74, rng.choice([rng.randrange(16), rng.getrandbits(32)]), 4),
              (QLO_DATA + 0x94, rng.randrange(5), 4),
              (QLO_DATA + 0xa4, rng.choice([0x22, 0x21, 2, 3, 0x1e, 5, 0xe, 0x19, 0x24, 0x18, 0x33,
                                            rng.randrange(0x40)]), 4),
              (cell + 0x07, rng.choice([rng.randrange(0x24), rng.randrange(256)]), 1)]
    if rng.random() < 0.5:
        fields += [(QLO_DATA + 0x264, args[1], 4), (QLO_DATA + 0x268, args[2], 4)]
    if rng.random() < 0.7:
        fields.append((cell + 0x02, rng.choice([0x50, 0x50, 2, 7, 8, 1, rng.randrange(10)]), 1))
    if rng.random() < 0.4:
        fields.append((cell + 0x0a, 0, 2))
    if rng.random() < 0.6:  # the direction's parameter above 72 (the water animations)
        fields.append((cell + 0x05, rng.randrange(19, 64), 1))
    if rng.random() < 0.4:  # the selection sprites: mode 1, a group and item, a marker
        fields += [(QLO_DATA + 0x60, 1, 4), (QLO_DATA + 0x64, rng.randrange(1, 5), 4),
                   (QLO_DATA + 0x68, rng.randrange(1, 13), 4), (cell + 0x07, rng.randrange(1, 0x21), 1)]
    if args[5] == 0 and rng.random() < 0.8:  # the water animations: a clock that puts a hash into
        fields += qlo_water_clock(rng, args, cell)  # (or next to) a time window
    return fields


QLO_WATER_STARTS = (0, 2000, 3100, 8000, 11500, 13000)
QLO_WAVE_STARTS = (0, 5000, 13000, 18000, 26000, 35000, 41000, 50000, 60000, 61000, 64000, 80000, 84000,
                   90000, 95000, 100000, 105000, 113000, 125000, 131000)


def qlo_water_clock(rng, args, cell):
    origin_column, origin_row, lift = rng.randrange(1024), rng.randrange(1024), rng.randrange(256)
    column, row = (origin_column + args[1]) & 0xFFFFFFFF, (origin_row + args[2]) & 0xFFFFFFFF
    first = rng.random() < 0.5
    terrain = 0x50 if first else rng.choice([2, 5, 7, 0x50])
    if first:  # the first animation (unk02 0x50)
        mixed = ((column * 0x521) ^ (-(row * 0x23b)) ^ (lift * 0x1677)) & 0xFFFFFFFF
        target, mask = rng.choice(QLO_WATER_STARTS) + rng.randrange(-1, 16), 0x3FFF
    else:  # the second one (level from +0xc; level 0 has all windows)
        mixed = ((column * 0x4521) ^ (-(row * 0x23b))) & 0xFFFFFFFF
        target, mask = rng.choice(QLO_WAVE_STARTS) + rng.randrange(-1, 16), 0x1FFFF
    quarter = (target - mixed) & mask
    clock = ((quarter + (rng.getrandbits(14) << 17)) << 2 | rng.randrange(4)) & 0xFFFFFFFF
    result = [(QLO_DATA + 0x24, origin_column, 4), (QLO_DATA + 0x28, origin_row, 4), (cell + 0x00, lift, 1),
            (QLO_DATA + 0x48, clock, 4), (cell + 0x0c, rng.choice([0xF, 0xE, rng.randrange(256)]), 1),
            (cell + 0x02, terrain, 1), (cell + 0x05, rng.randrange(19, 64), 1)]
    if rng.random() < 0.8:
        result.append((cell + 0x0a, 0, 2))
    return result


SPECS += [
    Spec("CDrawer_QueueLandscapeObject", 0x004d3050, "thiscall",
         [one_of(WORLD_BASE + QLO_DATA), below(130), below(130), anything, anything, qlo_object],
         presets=qlo_presets, callees=QLO_CALLEES, eax_mask=0, case_scale=3),
]


# -- CDrawer::QueueBuilding (drawer_buildings.cpp, session 59) ---------------
# Like QueueLandscapeObject's spec: the drawer at world 0, the draw data at
# QB_DATA, the animation getters probed (they return an image list at
# QB_LIST, lazily random but with small counts often), so the frame lookups
# (GfxDword::ListImageChecked / its copy 0x4b19c0 / ListImage, inlined in
# places) run on it; the queue functions, palette and sequence getters are
# probed (GetSequenceFrame writes a random frame record);
# Win32::ThrowAssertmentFailed ends the run as a throw (types outside
# 1..0x38 under construction). g_selectedBuilding is the cell's building
# number in half of the cases.
QB_DATA = 0x100000
QB_LIST = SCRATCH
QB_CELLS = QB_DATA + 0x500


def qb_list_out(m, ecx, args, h):
    m.guest_write(args[0], struct.pack("<I", WORLD_BASE + QB_LIST))
    return args[0]


def qb_sequence_frame(m, ecx, args, h):
    m.guest_write(args[0], struct.pack("<5I", *[(h >> (k * 7)) & 0xFFFFFFFF for k in range(5)]))
    return args[0]


QB_CALLEES = {
    0x004c7ab0: (1, "thiscall", lambda h: h & 3),  # Render::Entry204
    0x005fabb0: (2, "thiscall", "probe", qb_list_out, lambda m, ecx, a: (a[1],)),
    0x005fab50: (2, "thiscall", "probe", qb_list_out, lambda m, ecx, a: (a[1],)),
    0x005faaf0: (2, "thiscall", "probe", qb_list_out, lambda m, ecx, a: (a[1],)),
    0x005fac10: (2, "thiscall", "probe"),  # CGraphicsData::PalettePointer
    0x005fad20: (1, "thiscall", lambda h: 0xABCD0000 | (1 + h % 50)),  # SequenceFrameCount (AX)
    0x005fad50: (3, "thiscall", "probe", qb_sequence_frame, lambda m, ecx, a: (a[1], a[2])),
    0x004d7c90: (4, "thiscall", "probe"),  # CDrawer::QueueShadow
    0x004d7d50: (5, "thiscall", "probe"),  # CDrawer::QueueShadowPart
    0x004d7e10: (5, "thiscall", "probe"),  # CDrawer::QueueSprite
    0x004d7f30: (6, "thiscall", "probe"),  # CDrawer::QueueSpritePart
    0x004d8170: (5, "thiscall", "probe"),  # CDrawer::QueueColourLayer
    0x004d8050: (6, "thiscall", "probe"),  # CDrawer::QueueSpriteWithColour
    0x006119b0: (1, "thiscall", "throw"),  # Win32::ThrowAssertmentFailed
}


def qb_building(args):
    return (args[1] * 131 + args[2] * 7 + 5) & 0xFFFF


def qb_presets(rng, pool, args):
    cell = QB_CELLS + (args[1] * 130 + args[2]) * 0x30
    r = rng.random()
    progress = 0 if r < 0.45 else rng.choice([1, 2, 3]) if r < 0.55 else rng.choice([124, 125, 126, 255]) \
        if r < 0.65 else rng.randrange(256)
    fields = [(QB_LIST + 7, rng.choice([0, 1, 2, 3, 6, 40, rng.randrange(256)]), 1),
              (cell + 0x0c, rng.randrange(4), 1),
              (cell + 0x0d, rng.randrange(1, 0x39) if rng.random() < 0.97 else rng.randrange(256), 1),
              (cell + 0x10, progress, 1)]
    if rng.random() < 0.6:  # the direction's parameter above 72 (hit areas, smoke, sequences)
        fields.append((cell + 0x05, rng.randrange(19, 64), 1))
    if args[3] & 1:
        fields.append((cell + 0x0a, qb_building(args), 2))
    if rng.random() < 0.5:
        fields.append((cell + 0x11, rng.randrange(6), 1))
    if rng.random() < 0.5:
        fields.append((cell + 0x13, rng.choice([0, 1, 6, 7, 12, 13, 25, 30, rng.randrange(256)]), 1))
    if rng.random() < 0.5:
        fields.append((cell + 0x0e, rng.randrange(8), 1))
    for flag in range(6):
        if rng.random() < 0.3:
            fields.append((cell + 0x12 + flag, rng.choice([0, 0, 1]), 1))
    if rng.random() < 0.5:
        fields.append((cell + 0x1c, rng.choice([0, 0x20, 0x80, 0xff, rng.randrange(256)]), 1))
    if rng.random() < 0.3:
        fields.append((cell + 0x1d, rng.randrange(4), 1))
    return fields


def qb_globals(rng, pool, args):
    return [(0x007dfd6c, qb_building(args), 4)]  # g_selectedBuilding


SPECS += [
    Spec("CDrawer_QueueBuilding", 0x004a9960, "thiscall",
         [one_of(WORLD_BASE + QB_DATA), below(130), below(130), anything, anything],
         presets=qb_presets, globals=qb_globals, callees=QB_CALLEES, eax_mask=0, case_scale=3,
         exempt_blocks=(0x4b17ff,)),  # the alignment byte between the last ret and the jump table 0x4b1800
]


# -- Dirk::CTextSurface (text_surface.cpp, wip/text-r6) ---------------------
# The surface at world 0. The GDI calls are API probes (CreateCompatibleDC,
# SelectObject, SetBkMode, SetBkColor, SetTextColor, DrawTextA, DeleteDC,
# GdiFlush; each fails now and then the way its VERIFY tests).
# Win32::ThrowWin32Error 0x610de0 ends the run as a throw (WIN32_THROWERS,
# framework.py), compared with the message string's bytes, except where the
# original's allocator byte is an uninitialized stack byte (Lock; DrawText's
# DeleteDC check, line 0xb1): there the record leaves out byte 0. The font
# dictionary's find 0x60bab0 and insert 0x60bbe0 (Watcom code, not ours)
# are probed: find returns 0 (not there) or a pointer just past an HFONT
# slot in TS_FONTS, insert always one. The blocks after a thrower call are
# the original's never-reached clean-up of the message string.
TS_RESULT = SCRATCH                                   # the result rect
TS_RECT = SCRATCH + 1 * SCRATCH_SLOT_BYTES            # the rect argument (at an offset 0..255)
TS_STRING = SCRATCH + 2 * SCRATCH_SLOT_BYTES          # a std::string
TS_TEXT = SCRATCH + 3 * SCRATCH_SLOT_BYTES            # its characters / a format
TS_FONTS = SCRATCH + 4 * SCRATCH_SLOT_BYTES           # HFONT slots
TS_ARGUMENT = SCRATCH + 5 * SCRATCH_SLOT_BYTES        # a %s argument


def ts_thrower_record(machine, ecx, args):
    """thrower_record without the message's padding bytes 1..3 (the
    original writes only the allocator byte, the rest is whatever its
    frame held), and without the allocator byte too where that is an
    uninitialized stack byte in the original (Lock line 0xd4, DrawText
    line 0xb1)."""
    record = thrower_record(machine, ecx, args)
    message = record[-1]
    if message is not None:
        kept = message[4:] if args[0] in (0xb1, 0xd4) else message[:1] + message[4:]
        record = record[:-1] + (kept,)
    return record


TS_CALLEES = {
    0x00610de0: (3, "thiscall", "throw", None, ts_thrower_record),   # ThrowWin32Error(line, error, &message)
    0x0060bab0: (1, "thiscall", lambda h: 0 if h & 1 else WORLD_BASE + TS_FONTS + 4 + 8 * ((h >> 4) % 8)),
    0x0060bbe0: (1, "thiscall", lambda h: WORLD_BASE + TS_FONTS + 4 + 8 * ((h >> 4) % 8)),
}


def ts_rect_arg(rng, pool):
    return WORLD_BASE + TS_RECT + rng.randrange(256)


def ts_flags(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(128)


def ts_font(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.2 else rng.randrange(6)


def ts_string(rng, pool, args):
    """The std::string at TS_STRING: characters at TS_TEXT (sometimes none)."""
    length = rng.randrange(41)
    text = bytes(rng.randrange(32, 127) for _ in range(length))
    fields = [(TS_TEXT + i, b, 1) for i, b in enumerate(text + b"\0")]
    pointer = 0 if rng.random() < 0.1 else WORLD_BASE + TS_TEXT
    fields += [(TS_STRING + 4, pointer, 4), (TS_STRING + 8, length if pointer else 0, 4),
               (TS_STRING + 12, length, 4)]
    return fields


TS_FORMATS = [b"", b"Holz", b"%d", b"%u / %u", b"%s: %d", b"100%%", b"%s", b"Steine %d%%", b"%x %s %u",
              b"lang\nzweizeilig", b"%-5d|%05u"]


TS_ARGUMENT_2 = TS_ARGUMENT + 0x100


def ts_format_world(rng, pool, args):
    """DrawTextF: a format at TS_TEXT (picked by args[9], an extra argument
    vsprintf never reads) and texts at TS_ARGUMENT / TS_ARGUMENT_2, which
    the first two variable arguments point at (so a %s there is valid; a
    number conversion prints the pointer)."""
    fmt = TS_FORMATS[args[9] % len(TS_FORMATS)]
    fields = [(TS_TEXT + i, b, 1) for i, b in enumerate(fmt + b"\0")]
    for where in (TS_ARGUMENT, TS_ARGUMENT_2):
        argument = rng.choice([b"", b"Baumeister", b"a", b"Kohle und Eisen"])
        fields += [(where + i, b, 1) for i, b in enumerate(argument + b"\0")]
    return fields


def ts_format_index(rng, pool):
    return rng.randrange(len(TS_FORMATS))


# Unreachable after a thrower call (it never returns): the clean-up of the
# message string and the frees of temporaries that never get a buffer.
CLEANUP_CTEXTSURFACE_CREATE = (0x4a7ab8, 0x4a7adf, 0x4a7ae1, 0x4a7ae5, 0x4a7b0e, 0x4a7b18, 0x4a7b1c, 0x4a7c52, 0x4a7c74,
                               0x4a7c76, 0x4a7c7a, 0x4a7ca3, 0x4a7cad)
CLEANUP_CTEXTSURFACE_LOCK = (0x4a8cb3, 0x4a8cbd, 0x4a8cc1, 0x4a8cc7, 0x4a8d1e, 0x4a8d35, 0x4a8d3c, 0x4a8d41,
                             0x4a8d6d, 0x4a8d75, 0x4a8d77, 0x4a8d7b, 0x4a8da1, 0x4a8dad, 0x4a8db5, 0x4a8dba,
                             0x4a8dc4, 0x4a8dc8, 0x4a8dce)
CLEANUP_CTEXTSURFACE_DRAWTEXT = (0x4a81f6, 0x4a82f4, 0x4a83e9, 0x4a84de, 0x4a85d2, 0x4a86f6, 0x4a877f)
# DrawTextF: the string built from the buffer is never shared when it is
# freed: the temporary's buffer has count 1 when it is released (0x4a88db:
# count 0 / frozen), and the string's destructor finds count 0, so its
# split (0x4a896a, 0x4a896f) and the shared release (0x4a89f0, 0x4a89f4)
# don't run; erase(0, npos) leaves length 0, for which _Grow returns false
# (0x4a89d1).
CLEANUP_CTEXTSURFACE_DRAWTEXTF = (0x4a88db, 0x4a896a, 0x4a896f, 0x4a89d1, 0x4a89f0, 0x4a89f4)

SPECS += [
    Spec("CTextSurface_Unlock", 0x004a8e00, "thiscall", [], eax_mask=0),
    Spec("CTextSurface_Lock", 0x004a8bd0, "thiscall", [], callees=TS_CALLEES,
         exempt_blocks=CLEANUP_CTEXTSURFACE_LOCK),
    Spec("CTextSurface_DrawText", 0x004a8090, "thiscall",
         [one_of(WORLD_BASE + TS_RESULT), ts_font, ts_rect_arg, ts_flags, one_of(WORLD_BASE + TS_STRING)],
         presets=ts_string, callees=TS_CALLEES, exempt_blocks=CLEANUP_CTEXTSURFACE_DRAWTEXT),
    # cdecl (this, result, font, rect, flags, format, ...): "stdcall" passes
    # everything on the stack; both pop nothing. The nested DrawText runs.
    Spec("CTextSurface_DrawTextF", 0x004a87d0, "stdcall",
         [one_of(WORLD_BASE), one_of(WORLD_BASE + TS_RESULT), ts_font, ts_rect_arg, ts_flags,
          one_of(WORLD_BASE + TS_TEXT), one_of(WORLD_BASE + TS_ARGUMENT), one_of(WORLD_BASE + TS_ARGUMENT_2),
          anything, ts_format_index],
         presets=ts_format_world, callees=TS_CALLEES, exempt_blocks=CLEANUP_CTEXTSURFACE_DRAWTEXTF),
]


# -- the drawer's panel texts (text_panels.cpp, wip/text-r6) ----------------
# ClearTextRect / BlitTextRect: the drawer at world 0, its text surface
# (+0x5f6e0) at TP_SURFACE with bits at TP_BITS and a pitch of 1..24; Lock
# (GdiFlush) and Unlock run (ours are verified); a failing GdiFlush throws
# (TS_CALLEES). DrawGlyphRow 0x4d6950 is probed (verified; it writes
# pixels through a random pointer). Rects stay mostly inside the surface;
# sometimes the width is 0 or the end row wraps.
# The panel helpers: everything they call is probed: ClearTextRect,
# BlitTextRect, TextAddress (returns one of the texts at TP_TEXTS),
# DrawText and DrawTextF (cdecl; recorded without the result pointer, a
# stack local, and with texts by contents). g_uiCellSize and the drawing
# origin are random per case. The sprintf of PanelHundredths runs (CRT).
TP_SURFACE = SCRATCH + 6 * SCRATCH_SLOT_BYTES
TP_BITS = SCRATCH + 7 * SCRATCH_SLOT_BYTES
TP_TEXTS = SCRATCH + 8 * SCRATCH_SLOT_BYTES
TP_STRING_BUFFER = SCRATCH + 9 * SCRATCH_SLOT_BYTES
TP_GAME_TEXTS = [b"Holz", b"%u Steine", b"", b"Bauplatz\nfrei", b"%d%%", b"Kohle: %u"]


def tp_surface_world(rng, pool, args):
    pitch = rng.randrange(1, 25)
    return [(0x5f6e0, WORLD_BASE + TP_SURFACE, 4), (TP_SURFACE + 0x88, WORLD_BASE + TP_BITS, 4),
            (TP_SURFACE + 0x8c, pitch, 4)]


def tp_x(rng, pool):
    return rng.randrange(192)


def tp_width(rng, pool):
    return rng.choice([0, 1, rng.randrange(1, 9), rng.randrange(1, 192), rng.randrange(1, 64)])


def tp_y(rng, pool):
    return rng.randrange(4, 48)


def tp_height(rng, pool):
    return rng.choice([0, rng.randrange(1, 8), rng.randrange(48), 0xFFFFFFFF - rng.randrange(4)])


TP_RECT_CALLEES = dict(TS_CALLEES)
TP_RECT_CALLEES[0x004d6950] = (3, "thiscall", "probe")  # CDrawer::DrawGlyphRow


def tp_text_address(h):
    return WORLD_BASE + TP_TEXTS + 0x40 * (h % len(TP_GAME_TEXTS))


def tp_texts_world(rng, pool, args):
    fields = []
    for k, text in enumerate(TP_GAME_TEXTS):
        fields += [(TP_TEXTS + 0x40 * k + i, b, 1) for i, b in enumerate(text + bytes(1))]
    return fields


def tp_cstring(machine, address):
    return machine.guest_read(address, 64).split(bytes(1))[0] if address else None


def tp_drawtextf_record(varargs):
    def record(machine, ecx, args):
        return (args[0], args[2], args[3], args[4], tp_cstring(machine, args[5]), *args[6:6 + varargs])
    return record


def tp_string_record(machine, ecx, args):
    text = args[4]
    pointer, length = struct.unpack("<II", machine.guest_read(text + 4, 8))
    contents = machine.guest_read(pointer, min(length, 64)) if pointer else None
    return (args[1], args[2], args[3], length, contents)


def tp_panel_callees(varargs):
    return {
        0x004d65e0: (4, "thiscall", "probe"),   # CDrawer::ClearTextRect
        0x004d6740: (4, "thiscall", "probe"),   # CDrawer::BlitTextRect
        0x005faaa0: (2, "thiscall", tp_text_address),  # CGraphicsData::TextAddress
        0x004a87d0: (6 + varargs, "cdecl", lambda h: 0, None, tp_drawtextf_record(varargs)),  # DrawTextF
        0x004a8090: (5, "thiscall", lambda h: 0, None, tp_string_record),  # CTextSurface::DrawText
    }


def tp_layout_globals(rng, pool, args):
    cell = rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(3, 6)
    x = rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(800)
    y = rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(600)
    return [(0x00730F38, cell, 4), (0x00730F3C, x, 4), (0x00730F40, y, 4)]


def tp_small(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(60)


def tp_hundredths(rng, pool):
    r = rng.randrange(4)
    return (100 * rng.randrange(1000), 10 * rng.randrange(10000), rng.getrandbits(32), rng.randrange(100000))[r]


def tp_string_world(rng, pool, args):
    """PanelString's by-value string (args[2..5]): its buffer at
    TP_STRING_BUFFER (count byte, text), shared sometimes (count 1..3), so
    the destructor's split runs."""
    fields = tp_texts_world(rng, pool, args)
    text = rng.choice([b"Lager", b"Ein langer Titel", b"x"])
    count = rng.choice([0, 0, 1, 2, 3])
    fields.append((TP_STRING_BUFFER, count, 1))
    fields += [(TP_STRING_BUFFER + 1 + i, b, 1) for i, b in enumerate(text + bytes(1))]
    return fields


def tp_string_pointer(rng, pool):
    return 0 if rng.random() < 0.1 else WORLD_BASE + TP_STRING_BUFFER + 1


def tp_string_length(rng, pool):
    return rng.choice([1, 5, 16])


def tp_caption(rng, pool):
    return rng.choice([0, WORLD_BASE + TP_TEXTS, WORLD_BASE + TP_TEXTS + 0x40, WORLD_BASE + TP_TEXTS + 0xc0])


# The C runtime's floating-point conversion table (_cfltcvt_tab 0x7a5d24..):
# the CRT's start-up (_cfltcvt_init 0x61ef81) points its six entries at the
# conversion functions; the image still has _fptrap (0x62b7bc, "floating
# point not loaded") there, and the emulator doesn't run the start-up. The
# values are the ones _cfltcvt_init stores.
CRT_FLOAT_CONVERSIONS = [(0x007A5D24, 0x0062498C, 4), (0x007A5D28, 0x00624636, 4), (0x007A5D2C, 0x0062469C, 4),
                         (0x007A5D30, 0x006245DC, 4), (0x007A5D34, 0x00624684, 4), (0x007A5D38, 0x0062498C, 4)]


def tp_hundredths_globals(rng, pool, args):
    return tp_layout_globals(rng, pool, args) + CRT_FLOAT_CONVERSIONS


# Dead in the string helpers (as in CTextSurface::DrawTextF): a string
# that is freed is never shared any more (count 0 after the split or the
# construction; frozen ones skip both), so the shared release
# (PanelString 0x4d6d94, 0x4d6d99; PanelCaption 0x4d786b, 0x4d7870), the
# temporary's free with count 0 (PanelCaption 0x4d778a) and the split of an
# unshared string (PanelCaption 0x4d77de, 0x4d77e3) don't run; erase(0,
# npos) leaves length 0, for which _Grow returns false (PanelString
# 0x4d6d75, PanelCaption 0x4d7845).
DEAD_PANEL_STRING = (0x4d6d75, 0x4d6d94, 0x4d6d99)
DEAD_PANEL_CAPTION = (0x4d778a, 0x4d77de, 0x4d77e3, 0x4d7845, 0x4d786b, 0x4d7870)


def tp_panel_spec(name, address, args, varargs, **options):
    return Spec(name, address, "thiscall", args, presets=options.pop("presets", tp_texts_world),
                globals=options.pop("globals", tp_layout_globals), callees=tp_panel_callees(varargs),
                eax_mask=options.pop("eax_mask", 0), **options)


SPECS += [
    Spec("CDrawer_ClearTextRect", 0x004d65e0, "thiscall", [tp_x, tp_y, tp_width, tp_height],
         presets=tp_surface_world, callees=TP_RECT_CALLEES, eax_mask=0),
    Spec("CDrawer_BlitTextRect", 0x004d6740, "thiscall", [tp_x, tp_y, tp_width, tp_height],
         presets=tp_surface_world, callees=TP_RECT_CALLEES, eax_mask=0),
    tp_panel_spec("CDrawer_PanelText", 0x004d6a30, [tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextFont1", 0x004d6b10, [tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextLeftHalf", 0x004d6f90, [tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextRightHalf", 0x004d7070, [tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextArg", 0x004d6dd0, [tp_small, tp_small, anything, anything], 1),
    tp_panel_spec("CDrawer_PanelTextIndentedArg", 0x004d6eb0, [tp_small, tp_small, anything, anything], 1),
    tp_panel_spec("CDrawer_PanelTextAt", 0x004d7470, [tp_small, tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextAtFont1", 0x004d7550, [tp_small, tp_small, tp_small, anything], 0),
    tp_panel_spec("CDrawer_PanelTextAtArg", 0x004d78d0, [tp_small, tp_small, tp_small, anything, anything], 1),
    tp_panel_spec("CDrawer_PanelNumber", 0x004d7150, [tp_small, tp_small, anything], 1),
    tp_panel_spec("CDrawer_PanelPercent", 0x004d7390, [tp_small, tp_small, anything], 1),
    tp_panel_spec("CDrawer_PanelHundredths", 0x004d7230, [below(3), tp_small, tp_small, tp_hundredths], 0,
                  eax_mask=0xFFFFFFFF, globals=tp_hundredths_globals),
    tp_panel_spec("CDrawer_PanelString", 0x004d6bf0,
                  [tp_small, tp_small, anything, tp_string_pointer, tp_string_length, tp_string_length], 0,
                  presets=tp_string_world, exempt_blocks=DEAD_PANEL_STRING),
    tp_panel_spec("CDrawer_PanelCaption", 0x004d7630, [tp_small, tp_caption], 0, exempt_blocks=DEAD_PANEL_CAPTION),
]


# -- Dirk::CTextSurface::AddFont (text_surface.cpp, wip/text-r6) -------------
# GetStockObject (fails now and then: the VERIFY throw) and
# CreateFontIndirectA (records the LOGFONTA's 60 bytes) are API probes; the
# dictionary find/insert are probed as for DrawText; GameConfig::Dword28
# 0x4120a0 is probed (7, the Japanese charset, in half of the calls).
def ts_face(rng, pool, args):
    """AddFont's face: a std::string at TS_STRING with at most 31 characters
    (the original strcpy's it into LOGFONTA's 32 bytes)."""
    length = rng.randrange(32)
    text = bytes(rng.randrange(32, 127) for _ in range(length))
    fields = [(TS_TEXT + i, b, 1) for i, b in enumerate(text + bytes(1))]
    pointer = 0 if rng.random() < 0.1 else WORLD_BASE + TS_TEXT
    return fields + [(TS_STRING + 4, pointer, 4), (TS_STRING + 8, length, 4), (TS_STRING + 12, length, 4)]


TS_ADD_FONT_CALLEES = dict(TS_CALLEES)
TS_ADD_FONT_CALLEES[0x004120a0] = (0, "thiscall", lambda h: 7 if h & 1 else (h >> 4) % 10)  # GameConfig::Dword28

SPECS += [
    Spec("CTextSurface_AddFont", 0x004a7df0, "thiscall",
         [ts_font, one_of(WORLD_BASE + TS_STRING), anything, anything, anything],
         presets=ts_face, callees=TS_ADD_FONT_CALLEES,
         # the never-reached clean-up after the thrower; 0x4a8061: a failed
         # insert (the original writes to address 0), which the probe never gives
         exempt_blocks=(0x4a7f2e, 0x4a7f50, 0x4a7f52, 0x4a7f56, 0x4a7f80, 0x4a7f8a, 0x4a8061)),
]


# -- CDrawer::InitPanelFonts (text_panels.cpp, wip/text-r6) -----------------
# The drawer at world 0; CSoundData::GetSetNumber 0x5ff4e0 probed (4 in
# half of the calls: the MS Sans Serif branch), AddFont probed with the
# face recorded by contents; the face strings are built and freed for real
# (the bump allocator compares the allocations). g_uiCellSize, the drawing
# origin and the display globals are random per case.
def ts_face_record(machine, ecx, args):
    pointer, length = struct.unpack("<II", machine.guest_read(args[1] + 4, 8))
    face = machine.guest_read(pointer, min(length, 64)) if pointer else None
    return (args[0], length, face, *args[2:])


def tp_fonts_globals(rng, pool, args):
    return tp_layout_globals(rng, pool, args) + [(0x00730F0C, rng.getrandbits(32), 4),
                                                 (0x00730F10, rng.getrandbits(32), 4),
                                                 (0x00730F24, rng.getrandbits(32), 4)]


TP_FONTS_CALLEES = {
    0x005ff4e0: (0, "thiscall", lambda h: 4 if h & 1 else (h >> 4) % 8),  # CSoundData::GetSetNumber
    0x004a7df0: (5, "thiscall", "probe", None, ts_face_record),           # CTextSurface::AddFont
}

SPECS += [
    Spec("CDrawer_InitPanelFonts", 0x004c6f60, "thiscall", [], globals=tp_fonts_globals,
         callees=TP_FONTS_CALLEES, eax_mask=0),
]


# -- Dirk::CTextSurface::Create (text_surface.cpp, wip/text-r6) --------------
# GetDC, CreateDIBSection (records the BITMAPINFO, writes the bits
# pointer) and ReleaseDC are API probes; GetDC and CreateDIBSection fail
# now and then (the VERIFY throws, both messages without their allocator
# byte: uninitialized in the original). Widths around multiples of 32 and
# negative ones (the signed pitch division).
def ts_create_record(machine, ecx, args):
    record = thrower_record(machine, ecx, args)
    message = record[-1]
    return record[:-1] + ((message[4:] if message is not None else None),)


TS_CREATE_CALLEES = {0x00610de0: (3, "thiscall", "throw", None, ts_create_record)}


def ts_width(rng, pool):
    return rng.choice([rng.randrange(1, 2048), 32 * rng.randrange(1, 64), 32 * rng.randrange(1, 64) + 1,
                       -rng.randrange(1, 100) & 0xFFFFFFFF, 0, rng.getrandbits(32)])


SPECS += [
    Spec("CTextSurface_Create", 0x004a78f0, "thiscall", [ts_width, anything], callees=TS_CREATE_CALLEES,
         exempt_blocks=CLEANUP_CTEXTSURFACE_CREATE),
]


# -- Dirk::CTextSurface::DrawWideText (text_surface.cpp, wip/text-r6) --------
# The wide string at TS_STRING with its characters at TS_TEXT (1..0xff:
# characters wcstombs rejects set errno, whose thread data needs
# TlsGetValue, and an embedded zero hangs the original, see
# wide_to_narrow.cpp); WideToNarrowString and the CRT's wcstombs run;
# DrawText is probed (recorded with its text by contents, the result
# pointer being a stack local).
def ts_wide_string(rng, pool, args):
    length = rng.randrange(33)
    fields = []
    for i in range(length):
        c = rng.randrange(32, 127) if rng.random() < 0.9 else rng.randrange(1, 0x100)
        fields.append((TS_TEXT + 2 * i, c, 2))
    fields.append((TS_TEXT + 2 * length, 0, 2))
    pointer = 0 if rng.random() < 0.1 else WORLD_BASE + TS_TEXT
    return fields + [(TS_STRING + 4, pointer, 4), (TS_STRING + 8, length if pointer else 0, 4),
                     (TS_STRING + 12, length, 4)]


SPECS += [
    Spec("CTextSurface_DrawWideText", 0x004a8a20, "thiscall",
         [one_of(WORLD_BASE + TS_RESULT), ts_font, ts_rect_arg, ts_flags, one_of(WORLD_BASE + TS_STRING)],
         presets=ts_wide_string,
         callees={0x004a8090: (5, "thiscall", lambda h: WORLD_BASE + TS_RESULT + 0x40, None, tp_string_record)},
         # dead: erase(0, npos) leaves length 0, for which _Grow returns false
         # (0x4a8abd, 0x4a8b8f); the copy is never shared when it is destroyed
         # (the temporary's destructor split off its own buffer first: 0x4a8b2f,
         # 0x4a8b33)
         exempt_blocks=(0x4a8abd, 0x4a8b2f, 0x4a8b33, 0x4a8b8f)),
]


# -- CDrawer::RenderMessageTexts (text_panels.cpp, wip/text-r6) --------------
# The drawer at world 0, the draw data at TP_DRAW_DATA (lazily random, the
# shown flags preset half the time); ClearTextRect and DrawWideText probed
# (DrawWideText recorded with its rect's contents, a stack local).
TP_DRAW_DATA = 0x200000


def tp_draw_wide_record(machine, ecx, args):
    rect = struct.unpack("<4I", machine.guest_read(args[2], 16))
    return (args[1], rect, args[3], args[4])


def tp_message_world(rng, pool, args):
    fields = [(TP_DRAW_DATA + 0xc66d4, rng.choice([0, 0, 1, rng.randrange(256)]), 1)]
    for k in range(20):
        record = TP_DRAW_DATA + 0xc66d8 + 0x1c * k
        fields.append((record + 0x18, rng.choice([0, 0, 1, rng.randrange(256)]), 1))
        if rng.random() < 0.3:
            fields.append((record + 0x10, 0, 4))
    return fields


def tp_message_globals(rng, pool, args):
    cell = rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(3, 6)
    height = rng.getrandbits(32) if rng.random() < 0.1 else rng.choice([480, 600, 768])
    return [(0x00730F38, cell, 4), (0x00730F10, height, 4)]


SPECS += [
    Spec("CDrawer_RenderMessageTexts", 0x004d82b0, "thiscall", [one_of(WORLD_BASE + TP_DRAW_DATA)],
         presets=tp_message_world, globals=tp_message_globals, eax_mask=0,
         callees={0x004d65e0: (4, "thiscall", "probe"),
                  0x004a8a20: (5, "thiscall", lambda h: 0, None, tp_draw_wide_record)}),
]


# -- CDrawer::BlitMessageTexts (text_panels.cpp, wip/text-r6) ----------------
# The drawer at world 0, the draw data at TP_DRAW_DATA (shown flags, +0x10
# and the kinds preset half the time, header text length often 0);
# BlitTextRect probed; the animation getters probed (they write the image
# list TP_ICON_LIST, whose image offsets are lazily random); the palette
# getter probed; DrawSprite / DrawColorIndexed probed as stdcall (the
# GfxDword is a stack local: recorded by its value, the position and the
# clip rect by contents); GfxDword's constructor, ListImage, Point16 and
# wcslen run.
TP_ICON_LIST = SCRATCH + 10 * SCRATCH_SLOT_BYTES


def tp_icon_out(m, ecx, args, h):
    m.guest_write(args[0], struct.pack("<I", WORLD_BASE + TP_ICON_LIST))
    return args[0]


def tp_draw_record(machine, ecx, args):
    from unicorn.x86_const import UC_X86_REG_ECX
    this = machine.uc.reg_read(UC_X86_REG_ECX)
    value = struct.unpack("<I", machine.guest_read(this, 4))[0]
    position = struct.unpack("<I", machine.guest_read(args[1], 4))[0]
    clip = struct.unpack("<4I", machine.guest_read(args[2], 16))
    return (value, args[0], position, clip, *args[3:])


def tp_blit_messages_world(rng, pool, args):
    fields = [(TP_DRAW_DATA + 0xc66d4, rng.choice([0, 0, 1, rng.randrange(256)]), 1),
              (TP_DRAW_DATA + 0xc66c8, rng.choice([0, 0, rng.randrange(8)]), 4),
              (TP_DRAW_DATA + 0xc66c4, WORLD_BASE + TP_TEXTS, 4)]
    # The header text: "ABC" or one different character (compared with
    # g_setupWideText, see tp_blit_messages_globals).
    for i, c in enumerate([0x41, 0x42, 0x43] if rng.random() < 0.7 else [0x41, 0x58, 0x43]):
        fields.append((TP_TEXTS + 2 * i, c, 2))
    if rng.random() < 0.5:
        fields.append((TP_DRAW_DATA + 0xc66d0, 0, 4))
    for k in range(20):
        record = TP_DRAW_DATA + 0xc66d8 + 0x1c * k
        fields.append((record + 0x18, rng.choice([0, 0, 1, rng.randrange(256)]), 1))
        if rng.random() < 0.3:
            fields.append((record + 0x10, 0, 4))
        fields.append((record + 0x14, rng.choice([rng.randrange(20), 0xfc, 0xfd, 0xfe, rng.getrandbits(32),
                                                  rng.randrange(300)]), 4))
    return fields


def tp_blit_messages_globals(rng, pool, args):
    """Also g_setupWideText (0x7a8248, empty in the image) with 1..3
    characters in half of the cases, so the header's compare loop runs; the
    header text (tp_blit_messages_world) is then often equal to it or a
    prefix."""
    fields = tp_message_globals(rng, pool, args) + [(0x00730F0C, rng.choice([640, 800, 1024]), 4)]
    if rng.random() < 0.5:
        for i, c in enumerate([0x41, 0x42, 0x43][:rng.randrange(1, 4)]):
            fields.append((0x007A8248 + 2 * i, c, 2))
        fields.append((0x007A8248 + 2 * 3, 0, 2))
    return fields


SPECS += [
    Spec("CDrawer_BlitMessageTexts", 0x004d8400, "thiscall", [one_of(WORLD_BASE + TP_DRAW_DATA)],
         presets=tp_blit_messages_world, globals=tp_blit_messages_globals, eax_mask=0,
         callees={0x004d6740: (4, "thiscall", "probe"),                    # CDrawer::BlitTextRect
                  # (the out pointer is a stack local: recorded is the id)
                  0x005faaf0: (2, "thiscall", "probe", tp_icon_out, lambda m, ecx, a: (a[1],)),  # AnimationPointer
                  0x005fab50: (2, "thiscall", "probe", tp_icon_out, lambda m, ecx, a: (a[1],)),  # ColorIndexed...
                  0x005fac10: (2, "thiscall", "probe"),                    # PalettePointer
                  0x005fadb0: (5, "stdcall", "probe", None, tp_draw_record),   # GfxDword::DrawSprite
                  0x005fc5f0: (4, "stdcall", "probe", None, tp_draw_record)}),  # GfxDword::DrawColorIndexed
]


# -- CLandscapeSurface::BlitVisibleArea (landscape_blit.cpp, wip/text-r6) ----
# The surface at world 0: size (height above the 40-pixel band, width not
# 0: otherwise both loop forever), scroll positions up to a few wraps; the
# blit function pointer +0x84 points at the probe slot 0x84 (3 stack
# arguments: count, source, destination; both arrays are stack locals,
# recorded by an effect with exactly `count` rects, and EDX = +0x70);
# the screen globals random per case.
def lb_world(rng, pool, args):
    width = rng.choice([rng.randrange(1, 200), rng.randrange(400, 4400)])
    height = rng.choice([rng.randrange(41, 240), rng.randrange(400, 4400)])
    scroll_x = rng.choice([rng.randrange(8), rng.randrange(width * 3 + 1)])
    scroll_y = rng.choice([rng.randrange(32), rng.randrange(height * 3 + 1)])
    return [(0x50, width, 4), (0x54, height, 4), (0x68, scroll_x, 4), (0x6c, scroll_y, 4),
            (0x84, VIRTUAL_CODE_SLOT_84, 4)]


VIRTUAL_CODE_SLOT_84 = FAKE_VTABLE + 0x1000 + 0x84  # emu.VIRTUAL_CODE + slot offset


def lb_globals(rng, pool, args):
    return [(0x00730F18, rng.choice([rng.randrange(5000), rng.randrange(504, 1104)]), 4),
            (0x00730F1C, rng.choice([rng.randrange(5000), rng.randrange(480, 780)]), 4),
            (0x00730F24, rng.choice([rng.getrandbits(32), rng.randrange(136, 217)]), 4),
            (0x007DFD58, rng.choice([0, 0, 0, rng.getrandbits(32)]), 4)]


def lb_record_rects(machine, ecx, args, h):
    from unicorn.x86_const import UC_X86_REG_EDX
    count = args[0]
    n = min(count, 4)
    machine.probe_calls.append(("rects", machine.uc.reg_read(UC_X86_REG_EDX), count,
                                bytes(machine.guest_read(args[1], 16 * n)), bytes(machine.guest_read(args[2], 16 * n))))


SPECS += [
    Spec("CLandscapeSurface_BlitVisibleArea", 0x0054c9d0, "thiscall", [], presets=lb_world, globals=lb_globals,
         virtuals={0x84: (3, {1: 0, 2: 0})}, virtual_effects={0x84: lb_record_rects}),
]


# -- CDrawThread::SignalFrameDone (draw_thread.cpp, wip/text-r6) -------------
# The thread object at world 0; Win32::CEvent::Set 0x42cf80 probed (it is
# verified; recorded with ECX = the event pointer +0x64).
SPECS += [
    Spec("CDrawThread_SignalFrameDone", 0x0042c810, "thiscall", [],
         callees={0x0042cf80: (0, "thiscall", "probe")},
         exempt_blocks=(0x42c818,)),  # the alignment padding after the tail jump
]


# -- Dirk::CMouseSurface::SetHotspot (mouse_surface.cpp, wip/text-r6) --------
# The surface at world 0; Enter/Leave of its section +0x4c run (ours are
# verified; Enter/LeaveCriticalSection are API probes).
SPECS += [
    Spec("CMouseSurface_SetHotspot", 0x00469130, "thiscall", [anything, anything]),
]


# -- Dirk::CMouseSurface::Clear (mouse_surface.cpp, wip/text-r6) -------------
# The surface at world 0 with its IDirectDrawSurface (+8) at MS_IFACE
# (vtable FAKE_VTABLE: GetSurfaceDesc 0x58 and Unlock 0x80 as COM probes
# returning DD_OK, DDERR_SURFACELOST or another error); CLock and the
# critical section run (API probes); CheckResult 0x5bf2e0 probed (returns
# its hr); CSurface::Lock 0x5bf050 probed: it writes a description of up to
# 24 x 24 pixels at MS_PIXELS into the (stack) DDSURFACEDESC.
MS_IFACE = SCRATCH + 11 * SCRATCH_SLOT_BYTES
MS_PIXELS = SCRATCH + 12 * SCRATCH_SLOT_BYTES


def ms_world(rng, pool, args):
    return [(8, WORLD_BASE + MS_IFACE, 4), (MS_IFACE, FAKE_VTABLE, 4)]


def ms_lock_effect(machine, ecx, args, h):
    width = (h >> 8) % 25
    height = (h >> 16) % 25
    pitch = 2 * (width + (h >> 24) % 24) + ((h >> 32) & 1)
    offset = 2 * ((h >> 40) % 16)
    machine.guest_write(args[1] + 8, struct.pack("<III", height, width, pitch))
    machine.guest_write(args[1] + 0x24, struct.pack("<I", WORLD_BASE + MS_PIXELS + offset))
    return 0


def ms_com_result(h):
    return (0, 0, 0x887601C2, 0x887601C2, 0x88760005)[h % 5]


SPECS += [
    Spec("CMouseSurface_Clear", 0x00469310, "thiscall", [], presets=ms_world,
         virtuals={0x58: (2, {1: 0x6c}, {"com": True, "returns": ms_com_result}),
                   0x80: (2, {}, {"com": True, "returns": ms_com_result})},
         callees={0x005bf2e0: (2, "thiscall", "probe", lambda m, ecx, args, h: args[0]),       # CheckResult
                  0x005bf050: (3, "thiscall", "probe", ms_lock_effect,                         # CSurface::Lock
                               lambda m, ecx, a: (a[0], a[2], bytes(m.guest_read(a[1], 0x6c))))}),
]
