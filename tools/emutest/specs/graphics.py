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
