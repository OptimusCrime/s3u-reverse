"""Emulator specs, part `thirdparty`: specs of the replacements in
src/s3re/thirdparty/ (libpng 1.0.2, zlib 1.1.3, PaintLib).

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.

Layout of a case: the png_struct at world offset 0 (TP_PNG), a png_info
at TP_INFO, buffers in the scratch slots. The callbacks a png_struct
holds (error_fn, warning_fn, read_data_fn) point at S3.EXE functions that
are probed as callees: PaintLib's own PNG error callback 0x5a4970, the
NoOp 0x59f200 (PaintLib's warning callback) and PaintLib's read callback
0x5a4610 (its effect fills the buffer). The C runtime's malloc, free,
calloc, fread and fprintf are probed; longjmp ends the run like a throw.
"""

TP_PNG = 0x0
TP_PNG_PTR = WORLD_BASE + TP_PNG
TP_INFO = 0x1000
TP_INFO_PTR = WORLD_BASE + TP_INFO
TP_BLOCKS = 0x300000          # where the probed malloc's blocks lie
TP_ERROR_CB = 0x005a4970      # PaintLib's PNG error callback (png_ptr, message)
TP_WARNING_CB = 0x0059f200    # the NoOp PaintLib installs as warning callback
TP_READ_CB = 0x005a4610       # PaintLib's PNG read callback (png_ptr, data, length)


def tp_c_string(machine, address: int, limit: int = 256) -> bytes:
    """The bytes of a C string up to its terminator (a stack buffer's
    contents past it differ between the two functions)."""
    out = bytearray()
    for k in range(limit):
        byte = machine.guest_read(address + k, 1)[0]
        if byte == 0:
            break
        out.append(byte)
    return bytes(out)


def tp_fill_read(machine, ecx, args, h):
    """The read callback's effect: `length` bytes derived from h; a 4-byte
    read (a chunk's CRC) is, half the time, the current CRC (png->crc), so
    png_crc_error's match branch is reached."""
    data, length = args[1], args[2] & 0xFFFF
    if length == 4 and h & 1:
        crc = int.from_bytes(machine.guest_read(TP_PNG_PTR + 0x108, 4), "little")
        machine.guest_write(data, crc.to_bytes(4, "big"))
        return None
    stream = bytes(((h >> (8 * (k % 8))) + k * 37) & 0xFF for k in range(length))
    if length:
        machine.guest_write(data, stream)
    return None


def tp_fread(machine, ecx, args, h):
    """fread(buffer, 1, count, file): fills the buffer, returns count or, an
    eighth of the time, one less (a short read)."""
    buffer, count = args[0], args[2] & 0xFFFF
    if count:
        machine.guest_write(buffer, bytes((h + k) & 0xFF for k in range(count)))
    return count - 1 if h % 8 == 0 else count


def tp_malloc_result(h: int) -> int:
    """The probed malloc: null an eighth of the time, else a block in the world."""
    if h % 8 == 0:
        return 0
    return WORLD_BASE + TP_BLOCKS + (h >> 8) % 16 * 0x10000


TP_CALLEES = {
    TP_ERROR_CB: (2, "cdecl", "probe", None, lambda m, ecx, a: (a[0], tp_c_string(m, a[1]))),
    TP_WARNING_CB: (2, "cdecl", "probe", None, lambda m, ecx, a: (a[0], tp_c_string(m, a[1]))),
    TP_READ_CB: (3, "cdecl", "probe", tp_fill_read, lambda m, ecx, a: (a[0], a[2])),
    0x006214D6: (3, "cdecl", "probe", None, lambda m, ecx, a: (a[0], a[1], tp_c_string(m, a[2]))),  # fprintf
    0x00621FC0: (2, "cdecl", "throw"),                     # longjmp
    0x00621784: (1, "cdecl", tp_malloc_result),            # malloc
    0x00621B7E: (1, "cdecl", "probe"),                     # free
    0x00622199: (2, "cdecl", "probe"),                     # calloc
    0x0062166D: (4, "cdecl", "probe", tp_fread),           # fread
}

TP_FLAG_CHOICES = (0, 0x100, 0x200, 0x300, 0x400, 0x800, 0xC00, 0xF00)
TP_NAME_BYTES = (ord("I"), ord("H"), ord("D"), ord("R"), ord("z"), ord("a"), ord("Z"), ord("A"),
                 41, 40, 91, 96, 97, 90, 122, 123, 0, 0xFF, 0x80)


def tp_png_struct(rng, chunk_letter: bool = False) -> list:
    """Presets of the png_struct fields the functions read."""
    name = [rng.choice(TP_NAME_BYTES) if rng.random() < 0.6 else rng.getrandbits(8) for _ in range(4)]
    if chunk_letter:  # png_chunk_warning: see its spec
        name[rng.randrange(4)] = ord("a") + rng.randrange(26)
    return [
        (TP_PNG + 0x40, rng.choice((0, TP_ERROR_CB, TP_ERROR_CB)), 4),
        (TP_PNG + 0x44, rng.choice((0, TP_WARNING_CB)), 4),
        (TP_PNG + 0x4c, rng.choice((0, rng.getrandbits(32))), 4),
        (TP_PNG + 0x50, rng.choice((0, TP_READ_CB, TP_READ_CB, TP_READ_CB)), 4),
        (TP_PNG + 0x64, (rng.getrandbits(32) & ~0xF00) | rng.choice(TP_FLAG_CHOICES), 4),
        (TP_PNG + 0xa4, WORLD_BASE + SCRATCH + 4 * SCRATCH_SLOT_BYTES, 4),
        (TP_PNG + 0xa8, rng.choice((1, 4, 0x40, rng.randint(1, 0x300))), 4),
        (TP_PNG + 0x114, int.from_bytes(bytes(name), "little"), 4),
        (TP_PNG + 0x11b, rng.choice((0, rng.getrandbits(8))), 1),
        (TP_PNG + 0x11f, rng.choice((1, 2, 4, 7, 8, 16, rng.getrandbits(8))), 1),
    ]


def tp_png_presets(rng, pool, args):
    return tp_png_struct(rng)


def tp_letter_presets(rng, pool, args):
    return tp_png_struct(rng, chunk_letter=True)


def tp_signature_presets(rng, pool, args):
    """Half the time the PNG signature (or most of it) at the buffer."""
    if rng.random() < 0.5:
        sig = bytearray(b"\x89PNG\r\n\x1a\n")
        if rng.random() < 0.5:
            sig[rng.randrange(8)] ^= 1 << rng.randrange(8)
        return [(SCRATCH, int.from_bytes(bytes(sig), "little"), 8)]
    return []


def tp_info_presets(rng, pool, args):
    """The png_info: bit depth not 0 (the original divides by the bytes
    per pixel), small colour types, widths near the 2 GB row limit."""
    presets = tp_png_struct(rng)
    presets += [
        (TP_INFO + 0x18, rng.choice((1, 2, 4, 8, 16, rng.randint(1, 255))), 1),
        (TP_INFO + 0x19, rng.choice((0, 2, 3, 4, 6, rng.getrandbits(8))), 1),
        (TP_INFO + 0x08, rng.getrandbits(32) & ~8 | rng.choice((0, 8)), 4),
        (TP_INFO + 0x00, rng.choice((1, 100, 0x7FFFFFFF // 3, 0x7FFFFFFF // 8 + 1, 0x20000000,
                                     rng.getrandbits(32))), 4),
    ]
    return presets


def tp_pointer_or_null(slot: int, offset: int = 0):
    """A scratch pointer, null a quarter of the time."""
    return lambda rng, pool: 0 if rng.random() < 0.25 else WORLD_BASE + SCRATCH + slot * SCRATCH_SLOT_BYTES + offset


def tp_spec(name, address, args, presets=tp_png_presets, **kwargs):
    kwargs.setdefault("callees", TP_CALLEES)
    return Spec(name, address, "stdcall", args, presets=presets, **kwargs)


TP_PNG_ARG = one_of(TP_PNG_PTR)
TP_BUFFER = scratch(0)
TP_MESSAGE = scratch(2)

SPECS += [
    # -- zlib_checksums.cpp ------------------------------------------------
    tp_spec("zlib_crc32", 0x005aa130, [anything, one_of(0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH + 3),
                                       below(0x1800)], presets=None),
    # Up to 12000 bytes: two and more 5552-byte rounds.
    tp_spec("zlib_adler32", 0x005b13f0, [anything, one_of(0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH + 1),
                                         lambda rng, pool: rng.choice((rng.randrange(40), rng.randrange(12000),
                                                                       5552, 5551, 5553, 11104))],
            presets=None, max_cases=600),
    tp_spec("zlib_zcalloc", 0x005b1520, [anything, anything, anything], presets=None),
    tp_spec("zlib_zcfree", 0x005b1540, [anything, anything], presets=None, eax_mask=0),

    # -- png_error.cpp -------------------------------------------------------
    tp_spec("png_error", 0x005a9e20, [TP_PNG_ARG, TP_MESSAGE]),
    tp_spec("png_warning", 0x005a9e60, [TP_PNG_ARG, TP_MESSAGE], eax_mask=0),
    tp_spec("png_format_buffer", 0x005a9ef0, [TP_PNG_ARG, scratch(1), one_of(0, WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES)],
            eax_mask=0),
    tp_spec("png_chunk_error", 0x005a9ea0, [TP_PNG_ARG, TP_MESSAGE]),
    # Chunk names with at least one letter: with four non-letters the
    # original's buffer overflows into its return address (known
    # difference, docs/todo/known-differences.md).
    tp_spec("png_chunk_warning", 0x005a9f80, [TP_PNG_ARG, TP_MESSAGE], presets=tp_letter_presets, eax_mask=0),
    tp_spec("png_set_error_fn", 0x005a9fe0, [TP_PNG_ARG, anything, anything, anything], eax_mask=0),

    # -- png_mem.cpp ---------------------------------------------------------
    tp_spec("png_create_struct", 0x005aa000, [one_of(0, 1, 2, 3, 0xFFFFFFFF, 1, 2)], presets=None),
    tp_spec("png_destroy_struct", 0x005aa050, [one_of(0, WORLD_BASE + SCRATCH)], presets=None, eax_mask=0),
    tp_spec("png_malloc", 0x005aa060, [one_of(0, TP_PNG_PTR, TP_PNG_PTR), one_of(0, 1, 0x20c, 0x8000)]),
    tp_spec("png_memcpy_check", 0x005aa0c0, [anything, scratch(0), scratch(1), below(0x400)], presets=None),
    tp_spec("png_memset_check", 0x005aa0f0, [anything, scratch(0), anything, below(0x400)], presets=None),
    tp_spec("png_zalloc", 0x005a4e30, [TP_PNG_ARG, one_of(1, 2, 3, 0x40), one_of(0, 1, 5, 0x100, 0x2000, 0x4001)],
            max_cases=400),
    tp_spec("png_zfree", 0x005a4ea0, [one_of(0, TP_PNG_PTR), one_of(0, WORLD_BASE + SCRATCH)], presets=None,
            eax_mask=0),
    tp_spec("png_create_info_struct", 0x005a4f30, [one_of(0, TP_PNG_PTR, TP_PNG_PTR)], presets=None),

    # -- png_core.cpp --------------------------------------------------------
    tp_spec("png_sig_cmp", 0x005a4de0, [one_of(WORLD_BASE + SCRATCH), below(10), below(11, 0.1)],
            presets=tp_signature_presets),
    tp_spec("png_reset_crc", 0x005a4ec0, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_calculate_crc", 0x005a4ee0, [TP_PNG_ARG, TP_BUFFER, below(0x200)], eax_mask=0),
    tp_spec("png_get_io_ptr", 0x005a5020, [TP_PNG_ARG]),
    tp_spec("png_read_data", 0x005a9ac0, [TP_PNG_ARG, TP_BUFFER, below(0x100)], eax_mask=0),
    tp_spec("png_set_read_fn", 0x005a9af0, [TP_PNG_ARG, anything, one_of(0, TP_READ_CB)], eax_mask=0),
    tp_spec("png_default_read_data", 0x005a9b50, [TP_PNG_ARG, TP_BUFFER, below(0x100)], eax_mask=0),
    tp_spec("png_get_uint_32", 0x005aa8d0, [scratch(0)], presets=None),
    tp_spec("png_crc_read", 0x005aa900, [TP_PNG_ARG, TP_BUFFER, below(0x100)], eax_mask=0),
    tp_spec("png_crc_error", 0x005aaa80, [TP_PNG_ARG]),
    # A chunk name with a letter: png_chunk_warning's overflow (see above).
    tp_spec("png_crc_finish", 0x005aa930, [TP_PNG_ARG, lambda rng, pool: rng.choice((0, 0, 1, 4, rng.randrange(0x900)))],
            presets=tp_letter_presets),
    tp_spec("png_set_bgr", 0x005a5dc0, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_set_strip_16", 0x005a61f0, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_set_expand", 0x005a6200, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_set_gray_to_rgb", 0x005a6210, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_set_packing", 0x005a5dd0, [TP_PNG_ARG], eax_mask=0),
    tp_spec("png_set_interlace_handling", 0x005a5df0, [TP_PNG_ARG]),
    tp_spec("png_set_filler", 0x005a5e20, [TP_PNG_ARG, anything, one_of(0, 1, 2, 0x101)], eax_mask=0),
    tp_spec("png_get_IHDR", 0x005a9990,
            [lambda rng, pool: 0 if rng.random() < 0.1 else TP_PNG_PTR,
             lambda rng, pool: 0 if rng.random() < 0.1 else TP_INFO_PTR,
             *(lambda rng, pool, k=k: 0 if rng.random() < 0.08 else WORLD_BASE + SCRATCH + 0x100 + 8 * k
               for k in range(4)),
             *(tp_pointer_or_null(0, 0x200 + 8 * k) for k in range(3))],
            presets=tp_info_presets),
    tp_spec("png_get_PLTE", 0x005a9a80,
            [lambda rng, pool: 0 if rng.random() < 0.1 else TP_PNG_PTR,
             lambda rng, pool: 0 if rng.random() < 0.1 else TP_INFO_PTR,
             tp_pointer_or_null(0, 0x100), scratch(1)],
            presets=tp_info_presets),
]


# -- png_rtran_rows.cpp ------------------------------------------------------
#
# The row_info at TP_ROW_INFO (mostly consistent colour type / depth /
# channels / rowbytes, sometimes anything), the row in the middle of a
# 0x6000-byte area (the expanding transforms walk backwards from up to
# 8 * width bytes past it, the inconsistent ones also before it).

TP_ROW_INFO = 0x2000
TP_ROW = SCRATCH + 0x8000
TP_ROW_PTR = WORLD_BASE + TP_ROW
TP_TABLES = SCRATCH + 0x10000      # palette / lookup tables
TP_TABLES_PTR = WORLD_BASE + TP_TABLES
TP_COLOUR = SCRATCH + 0x20000      # png_color_16 / png_color_8 arguments
TP_COLOUR_PTR = WORLD_BASE + TP_COLOUR
TP_CHANNELS = (1, 0, 3, 1, 2, 0, 4, 0)


def tp_row_info(rng, pool, args):
    colour_type = rng.choice((0, 2, 3, 4, 6)) if rng.random() < 0.9 else rng.randrange(8)
    depth = rng.choice((1, 2, 4, 8, 16)) if rng.random() < 0.9 else rng.randrange(18)
    channels = TP_CHANNELS[colour_type] or 1
    if rng.random() < 0.1:
        channels = rng.randint(1, 4)
    width = rng.choice((0, 1, 2, 3, 7, 8, 9)) if rng.random() < 0.3 else rng.randint(1, 120)
    pixel_depth = (channels * depth) & 0xFF
    rowbytes = (width * pixel_depth + 7) >> 3
    if rng.random() < 0.05:
        rowbytes = rng.randrange(0x400)
    presets = [(TP_ROW_INFO, width, 4), (TP_ROW_INFO + 4, rowbytes, 4), (TP_ROW_INFO + 8, colour_type, 1),
               (TP_ROW_INFO + 9, depth, 1), (TP_ROW_INFO + 10, channels, 1), (TP_ROW_INFO + 11, pixel_depth, 1)]
    if rng.random() < 0.5:
        # A transparent colour / significant bits that match pixels of the row.
        pixel = bytes(rng.getrandbits(8) for _ in range(6))
        for k in range(rng.randint(1, 4)):
            presets.append((TP_ROW + rng.randrange(0, 60) * 3, int.from_bytes(pixel, "little"), 6))
        if rng.random() < 0.5:  # 8-bit samples
            presets += [(TP_COLOUR + 2, pixel[0], 2), (TP_COLOUR + 4, pixel[1], 2), (TP_COLOUR + 6, pixel[2], 2)]
        else:  # 16-bit samples (big-endian in the row)
            presets += [(TP_COLOUR + 2 + 2 * k, (pixel[2 * k] << 8) | pixel[2 * k + 1], 2) for k in range(3)]
        presets.append((TP_COLOUR + 8, rng.choice((pixel[0], (pixel[0] << 8) | pixel[1])), 2))
        for k in range(3):
            presets.append((TP_ROW + rng.randrange(0, 60) * 6, int.from_bytes(pixel, "little"), 6))
    return presets


def tp_unshift_info(rng, pool, args):
    """Significant bits 1..depth (0 would shift 16-bit samples by 16, an
    undefined x86 shift count; libpng's sBIT handler never stores 0)."""
    presets = tp_row_info(rng, pool, args)
    depth = next(value for offset, value, size in presets if offset == TP_ROW_INFO + 9)
    bits = bytes(rng.randint(1, max(depth, 1) if rng.random() < 0.8 else 16) for _ in range(5))
    if depth == 16:
        bits = bytes(min(b, 16) or 1 for b in bits)
    return presets + [(TP_COLOUR, int.from_bytes(bits, "little"), 5)]


TP_ROW_INFO_ARG = one_of(WORLD_BASE + TP_ROW_INFO)
TP_ROW_ARG = one_of(TP_ROW_PTR)


def tp_row_spec(name, address, extra=(), presets=tp_row_info, **kwargs):
    return Spec(name, address, "stdcall", [TP_ROW_INFO_ARG, TP_ROW_ARG, *extra], presets=presets, eax_mask=0,
                max_cases=1000, **kwargs)


TP_FLAGS = one_of(0, 0x80, 0xFFFFFFFF, 0x7F)

SPECS += [
    tp_row_spec("png_do_invert", 0x005a5e60),
    tp_row_spec("png_do_swap", 0x005a5e90),
    tp_row_spec("png_do_packswap", 0x005a5ec0),
    tp_row_spec("png_do_strip_filler", 0x005a5f10, [TP_FLAGS]),
    tp_row_spec("png_do_bgr", 0x005a6120),
    tp_row_spec("png_do_unpack", 0x005a7030),
    tp_row_spec("png_do_unshift", 0x005a7160, [one_of(TP_COLOUR_PTR)], presets=tp_unshift_info),
    tp_row_spec("png_do_read_swap_alpha", 0x005a7340),
    tp_row_spec("png_do_read_invert_alpha", 0x005a7440),
    tp_row_spec("png_do_read_filler", 0x005a7550, [anything, TP_FLAGS]),
    tp_row_spec("png_do_gray_to_rgb", 0x005a77b0),
    tp_row_spec("png_do_expand_palette", 0x005a8cf0,
                [one_of(TP_TABLES_PTR), one_of(0, TP_TABLES_PTR + 0x300),
                 lambda rng, pool: rng.choice((0, 1, 16, 255, 256, 0xFFFFFFFF, rng.randrange(300)))]),
    tp_row_spec("png_do_expand", 0x005a8f20, [one_of(0, TP_COLOUR_PTR, TP_COLOUR_PTR)]),
    tp_row_spec("png_do_dither", 0x005a92b0, [one_of(0, TP_TABLES_PTR), one_of(0, TP_TABLES_PTR + 0x8000)]),
]


# -- png_set.cpp ---------------------------------------------------------------

def tp_png_or_null(rng, pool):
    return 0 if rng.random() < 0.1 else TP_PNG_PTR


def tp_info_or_null(rng, pool):
    return 0 if rng.random() < 0.1 else TP_INFO_PTR


def tp_double_half(rng, pool):
    """Half of a double argument: random bits, small integers' halves or 0."""
    return rng.choice((0, rng.getrandbits(32), 0x3FE00000, 0x40590000, 0xBFF00000, 0x7FF00000, 0x7FF80000))


def tp_ihdr_presets(rng, pool, args):
    return tp_png_struct(rng)


def tp_ihdr_depth(rng, pool):
    return rng.choice((1, 2, 4, 8, 16, rng.randint(1, 63), 0x101))


def tp_ihdr_width(rng, pool):
    return rng.choice((1, 100, 0x7FFFFFFF // 3, 0x7FFFFFFF // 3 + 1, 0x7FFFFFFF // 8, 0x7FFFFFFF // 8 + 1,
                       0x20000000, rng.getrandbits(32)))


def tp_double_high(rng, pool):
    """The high half of a double argument: like tp_double_half, but never a
    signalling NaN. The emulator's x87 (QEMU softfloat) keeps an SNaN
    signalling through fld/fstp, a real FPU quiets it (sets bit 22 of the
    float), as SSE does; the in-process test PngSetters_MatchOriginal
    compares SNaN arguments on the real CPU."""
    value = tp_double_half(rng, pool)
    if value & 0x7FF80000 == 0x7FF00000:
        value |= 0x00080000
    return value


TP_SET_HEAD = [tp_png_or_null, tp_info_or_null]

SPECS += [
    tp_spec("png_set_bKGD", 0x005b1550, [*TP_SET_HEAD, one_of(TP_COLOUR_PTR)], eax_mask=0),
    tp_spec("png_set_cHRM", 0x005b1590, [*TP_SET_HEAD, *([tp_double_half, tp_double_high] * 8)], eax_mask=0),
    tp_spec("png_set_gAMA", 0x005b1600, [*TP_SET_HEAD, tp_double_half, tp_double_high], eax_mask=0),
    tp_spec("png_set_hIST", 0x005b1630, [*TP_SET_HEAD, anything], eax_mask=0),
    # The colour types keep channels * depth away from 0 mod 256 (the
    # original divides by the bytes per pixel): depths below 64.
    tp_spec("png_set_IHDR", 0x005b1660,
            [*TP_SET_HEAD, tp_ihdr_width, anything, tp_ihdr_depth,
             lambda rng, pool: rng.choice((0, 2, 3, 4, 6, rng.getrandbits(8))), anything, anything, anything],
            presets=tp_ihdr_presets, eax_mask=0),
    tp_spec("png_set_oFFs", 0x005b1720, [*TP_SET_HEAD, anything, anything, anything], eax_mask=0),
    tp_spec("png_set_pHYs", 0x005b1890, [*TP_SET_HEAD, anything, anything, anything], eax_mask=0),
    tp_spec("png_set_PLTE", 0x005b18c0, [*TP_SET_HEAD, anything, anything], eax_mask=0),
    tp_spec("png_set_sBIT", 0x005b18f0, [*TP_SET_HEAD, one_of(TP_COLOUR_PTR)], eax_mask=0),
    tp_spec("png_set_sRGB_gAMA_and_cHRM", 0x005b1920, [*TP_SET_HEAD, anything], eax_mask=0),
    tp_spec("png_set_tIME", 0x005b1ab0, [*TP_SET_HEAD, one_of(TP_COLOUR_PTR)], eax_mask=0),
    tp_spec("png_set_tRNS", 0x005b1ae0, [*TP_SET_HEAD, one_of(0, TP_TABLES_PTR), one_of(0, 0, 1, 5, 0x10000),
                                         one_of(0, TP_COLOUR_PTR)], eax_mask=0),
]


# -- paintlib.cpp ----------------------------------------------------------------
#
# The object at world offset 0 (ECX = the world). Its vtable is the fake one
# (virtual calls recorded) where a function calls virtuals of its own object;
# constructors set the real vtables, so their nested virtual calls run the
# original PaintLib code in both. Pointers to free point into scratch (the
# fake heap records operator delete).

PL_VIRTUALS = {0x0c: 0, 0x10: 4, 0x14: 0, 0x18: 0}
PL_TRACE_FILE = 0x007E8BBC   # PaintLib's trace file name


def pl_trace_global(rng, pool, args):
    return [(PL_TRACE_FILE, rng.choice((0, WORLD_BASE + SCRATCH + 0x3000)), 4)]


def pl_bitmap(rng, pool, args, fake_vtable=True):
    """A CBmp / CAnyBmp: small sizes and depths (sometimes anything), the
    palette, bits and line array in scratch (or null)."""
    presets = [
        (0x04, rng.choice((rng.randint(0, 40), rng.getrandbits(32))), 4),
        (0x08, rng.choice((rng.randint(-2, 40), rng.randint(0, 3), rng.getrandbits(32) & 0x8000FFFF)), 4),
        (0x0c, rng.choice((1, 2, 4, 8, 15, 16, 24, 32, 0, rng.randint(0, 40), rng.getrandbits(16))), 2),
        (0x10, rng.choice((0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH)), 4),
        (0x14, rng.choice((0, WORLD_BASE + SCRATCH + 0x1000)), 4),
        (0x1c, rng.choice((0, WORLD_BASE + SCRATCH + 0x2000)), 4),
    ]
    if fake_vtable:
        presets.append((0x00, FAKE_VTABLE, 4))
    return presets


def pl_decoder(rng, pool, args):
    return [(0x10, rng.choice((0, 0, 8, 32, rng.getrandbits(32))), 4)]


def pl_source(rng, pool, args):
    return [(0x04, rng.choice((0, WORLD_BASE + SCRATCH + 0x1000)), 4),
            (0x0c, rng.choice((0, WORLD_BASE + SCRATCH + 0x2000)), 4)]


def pl_spec(name, address, args=(), presets=None, **kwargs):
    return Spec(name, address, "thiscall", list(args), presets=presets, **kwargs)


PL_DEPTH = lambda rng, pool: rng.choice((1, 2, 4, 8, 16, 24, 32, rng.randint(0, 40), rng.getrandbits(32)))
PL_SIZE = lambda rng, pool: rng.choice((0, 1, 2, 16, 33, rng.randint(-2, 64)))
PL_FLAGS = one_of(0, 1, 2, 3)

SPECS += [
    pl_spec("CPicDecoder_Construct", 0x005a4980),
    pl_spec("CPicDecoder_Destroy", 0x005a49d0, globals=pl_trace_global, eax_mask=0),
    pl_spec("CPicDecoder_DeletingDestructor", 0x005a4990, [PL_FLAGS], globals=pl_trace_global),
    pl_spec("CPicDecoder_CalcDestBPP", 0x005a4cc0,
            [lambda rng, pool: rng.choice((0, 1, 7, 8, 9, 16, 24, 32, 0xFFFFFFFF, rng.getrandbits(32)))],
            presets=pl_decoder),
    pl_spec("CBmp_Construct", 0x005a4ce0),
    pl_spec("CBmp_Destroy", 0x005a4d10, eax_mask=0),
    pl_spec("CBmp_DeletingDestructor", 0x005a4cf0, [PL_FLAGS]),
    pl_spec("CBmp_Create", 0x005a4d20, [PL_SIZE, PL_SIZE, PL_DEPTH, anything], presets=pl_bitmap,
            virtuals=PL_VIRTUALS),
    # The virtual initLineArray (fake) may change bpp: initLocals reads it again
    # (to depths below 16: 24 bits would fill 2^24 palette entries).
    pl_spec("CBmp_initLocals", 0x005a4d50, [PL_SIZE, PL_SIZE, PL_DEPTH, anything],
            presets=lambda rng, pool, args: [p for p in pl_bitmap(rng, pool, args) if p[0] != 0x10]
            + [(0x10, WORLD_BASE + SCRATCH, 4)],
            virtuals=PL_VIRTUALS,
            virtual_effects={0x18: lambda m, ecx, a, h: m.guest_write(WORLD_BASE + 0x0c, bytes([(h >> 1) & 0x0F, 0]))
                             if h & 1 else None},
            # 0x5a4d8a: bpp changed to 32 by initLineArray (2^24 palette entries,
            # too slow to emulate); the in-process test Paintlib_MatchOriginal
            # covers it (its fake initLineArray sets 32).
            exempt_blocks=(0x005a4d8a,), eax_mask=0, max_cases=500),
    pl_spec("CAnyBmp_Construct", 0x005a4350, max_cases=300),
    pl_spec("CAnyBmp_Destroy", 0x005a43e0, presets=pl_bitmap, eax_mask=0),
    pl_spec("CAnyBmp_DeletingDestructor", 0x005a43c0, [PL_FLAGS], presets=pl_bitmap),
    pl_spec("CAnyBmp_freeMembers", 0x005a4570, presets=pl_bitmap, eax_mask=0),
    pl_spec("CAnyBmp_GetMemUsed", 0x005a4460, presets=pl_bitmap),
    pl_spec("CAnyBmp_GetBytesPerLine", 0x005a44a0, presets=pl_bitmap),
    pl_spec("CAnyBmp_internalCreate", 0x005a44b0, [PL_SIZE, PL_SIZE, PL_DEPTH, anything],
            presets=pl_bitmap, virtuals=PL_VIRTUALS, eax_mask=0),
    pl_spec("CAnyBmp_initLineArray", 0x005a4520, presets=pl_bitmap, virtuals=PL_VIRTUALS),
    pl_spec("CTextException_Destroy", 0x005a4330,
            presets=lambda rng, pool, args: [(0x08, rng.choice((0, WORLD_BASE + SCRATCH)), 4)], eax_mask=0),
    # The copy constructor follows at 0x5a42c0 (not in the function lists):
    # coverage up to the padding.
    pl_spec("CTextException_DeletingDestructor", 0x005a4290, [PL_FLAGS],
            presets=lambda rng, pool, args: [(0x08, rng.choice((0, WORLD_BASE + SCRATCH)), 4)],
            cover_until=0x005a42bc),
    pl_spec("CDataSource_Construct", 0x005b03f0),
    pl_spec("CDataSource_Destroy", 0x005b0460, presets=pl_source, eax_mask=0),
    pl_spec("CDataSource_DeletingDestructor", 0x005b0410, [PL_FLAGS], presets=pl_source),
    # The name: a C string in scratch (random bytes up to a terminator).
    pl_spec("CDataSource_Attach", 0x005b04a0, [one_of(WORLD_BASE + SCRATCH + 0x4000), anything, anything],
            presets=lambda rng, pool, args: [(SCRATCH + 0x4000 + rng.randrange(40), 0, 1)]),
    pl_spec("CDataSource_Release", 0x005b0500, presets=pl_source),
]


# -- zlib_inflate_setup.cpp ---------------------------------------------------------
#
# A z_stream at ZL_STREAM whose zalloc / zfree are zlib's zcalloc / zcfree
# (run as they are) over the probed calloc (blocks in the world, null an
# eighth of the time) and free; an inflate state at ZL_STATE and a blocks
# state at ZL_BLOCKS whose check function is adler32 (run) or null.

ZL_STREAM = 0x3000
ZL_STREAM_PTR = WORLD_BASE + ZL_STREAM
ZL_STATE = 0x3100
ZL_BLOCKS = 0x3200
ZL_BLOCKS_PTR = WORLD_BASE + ZL_BLOCKS
ZL_ZCALLOC = 0x005b1520
ZL_ZCFREE = 0x005b1540
ZL_ADLER32 = 0x005b13f0
ZL_VERSION = 0x00733754   # "1.1.3" in S3.EXE

ZL_CALLEES = {
    0x00622199: (2, "cdecl", tp_malloc_result),   # calloc
    0x00621B7E: (1, "cdecl", "probe"),            # free
}


def zl_stream(rng, pool, args):
    return [
        (ZL_STREAM + 0x1c, rng.choice((0, WORLD_BASE + ZL_STATE, WORLD_BASE + ZL_STATE, WORLD_BASE + ZL_STATE)), 4),
        (ZL_STREAM + 0x20, rng.choice((ZL_ZCALLOC, ZL_ZCALLOC, 0)), 4),
        (ZL_STREAM + 0x24, rng.choice((ZL_ZCFREE, ZL_ZCFREE, 0)), 4),
        (ZL_STATE + 0x0c, rng.choice((0, 1, rng.getrandbits(32))), 4),
        (ZL_STATE + 0x14, rng.choice((0, ZL_BLOCKS_PTR, ZL_BLOCKS_PTR)), 4),
        (ZL_BLOCKS + 0x00, rng.choice((0, 1, 4, 5, 6, 7, 9)), 4),
        (ZL_BLOCKS + 0x04, WORLD_BASE + SCRATCH, 4),
        (ZL_BLOCKS + 0x0c, WORLD_BASE + SCRATCH + 0x100, 4),
        (ZL_BLOCKS + 0x38, rng.choice((0, ZL_ADLER32)), 4),
    ]


def zl_spec(name, address, args, presets=zl_stream, **kwargs):
    return Spec(name, address, "stdcall", args, presets=presets, callees=ZL_CALLEES, **kwargs)


ZL_STREAM_ARG = one_of(ZL_STREAM_PTR)
ZL_VERSION_ARG = lambda rng, pool: rng.choice((ZL_VERSION, ZL_VERSION, ZL_VERSION, 0, WORLD_BASE + SCRATCH))
ZL_SIZE_ARG = one_of(0x38, 0x38, 0x38, 0x34)

SPECS += [
    zl_spec("zlib_inflate_trees_fixed", 0x005b2a70,
            [scratch(0), scratch(1), scratch(2), scratch(3), ZL_STREAM_ARG], presets=None),
    zl_spec("zlib_inflate_codes_new", 0x005b1b50, [anything, anything, anything, anything, ZL_STREAM_ARG]),
    zl_spec("zlib_inflate_codes_free", 0x005b2340, [anything, ZL_STREAM_ARG], eax_mask=0),
    zl_spec("zlib_inflate_blocks_reset", 0x005b0520, [one_of(ZL_BLOCKS_PTR), ZL_STREAM_ARG, one_of(0, WORLD_BASE + SCRATCH + 0x200)],
            eax_mask=0),
    zl_spec("zlib_inflate_blocks_new", 0x005b05a0, [ZL_STREAM_ARG, one_of(0, ZL_ADLER32),
                                                     lambda rng, pool: rng.choice((1, 0x100, 0x8000, rng.randint(1, 0x8000)))],
            max_cases=600),
    zl_spec("zlib_inflate_blocks_free", 0x005b1360, [one_of(ZL_BLOCKS_PTR), ZL_STREAM_ARG]),
    # inflateReset with a blocks state (without one, both crash in blocks_reset).
    zl_spec("zlib_inflateReset", 0x005aa270, [one_of(0, ZL_STREAM_PTR, ZL_STREAM_PTR, ZL_STREAM_PTR)],
            presets=lambda rng, pool, args: zl_stream(rng, pool, args) + [(ZL_STATE + 0x14, ZL_BLOCKS_PTR, 4)]),
    zl_spec("zlib_inflateEnd", 0x005aa2c0, [one_of(0, ZL_STREAM_PTR, ZL_STREAM_PTR, ZL_STREAM_PTR)]),
    zl_spec("zlib_inflateInit2_", 0x005aa310,
            [one_of(0, ZL_STREAM_PTR, ZL_STREAM_PTR, ZL_STREAM_PTR, ZL_STREAM_PTR),
             lambda rng, pool: rng.choice((15, 15, -15, 8, 7, 16, -8, -16, 9, rng.randint(-20, 20))),
             ZL_VERSION_ARG, ZL_SIZE_ARG], max_cases=800,
            # 0x5aa451: the inlined inflateEnd's blocks_free after a bad
            # window size; dead, the state's blocks pointer was set to null
            # just before.
            exempt_blocks=(0x005aa451,)),
    zl_spec("zlib_inflateInit_", 0x005aa480, [one_of(0, ZL_STREAM_PTR, ZL_STREAM_PTR), ZL_VERSION_ARG, ZL_SIZE_ARG],
            max_cases=400),
]


# -- zlib_inflate.cpp ------------------------------------------------------------
#
# One inflate stream laid out in the world: the z_stream at ZI_Z (zalloc /
# zfree are zlib's zcalloc / zcfree over the probed calloc: blocks in the
# world, null one call in 40), the inflate state, the blocks state and a
# codes state after it, the huft space at ZI_HUFTS, the window at ZI_WINDOW
# (world before it, which reads of invalid distances reach), input at ZI_IN,
# output at ZI_OUT. The input comes from Python's zlib (every level and
# strategy, raw or with the zlib header, with flushes between parts, now and
# then corrupted or truncated) or is random. States in the middle of a
# stream are made up directly; their Huffman tables come from a Python port
# of huft_build (zi_huft_build). huft_build stores stack garbage in bytes
# 2..3 of every huft entry (see zlib_inflate.cpp), so those bytes of the huft
# space are not compared (ZI_HUFT_PADDING); the entries' compared bytes sit
# right next to them.
#
# The case is drawn by the first argument's generator and kept in _ZI for the
# other arguments and the presets (arguments are drawn in order, then the
# presets).

import zlib as _zlib

ZI_Z = 0x4000
ZI_STATE = 0x4100
ZI_BLOCKS = 0x4200
ZI_CODES = 0x4300
ZI_OUTS = 0x4400
ZI_LENS = 0x4800
ZI_V = 0x4e00
ZI_HUFTS = 0x6000
ZI_BLENS = 0x9000
ZI_WINDOW = 0x20000
ZI_IN = 0x30000
ZI_OUT = 0x40000
ZI_ALLOC = 0x50000
ZI_FIXED_TL = 0x007348E8
ZI_FIXED_TD = 0x007358E8
ZI_CPLENS, ZI_CPLEXT, ZI_CPDIST, ZI_CPDEXT = 0x0065C690, 0x0065C70C, 0x0065C788, 0x0065C800
ZI_CPLENS_V = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
               163, 195, 227, 258, 0, 0)
ZI_CPLEXT_V = (0,) * 8 + (1,) * 4 + (2,) * 4 + (3,) * 4 + (4,) * 4 + (5,) * 4 + (0, 112, 112)
ZI_CPDIST_V = (1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
               3073, 4097, 6145, 8193, 12289, 16385, 24577)
ZI_CPDEXT_V = (0, 0, 0, 0) + tuple(k // 2 for k in range(2, 28))
ZI_HUFT_PADDING = tuple(ZI_HUFTS + 8 * k + j for k in range(1440) for j in (2, 3))
_ZI = {}


def zi_calloc_result(h: int) -> int:
    if h % 40 == 0:
        return 0
    return WORLD_BASE + ZI_ALLOC + (h >> 8) % 32 * 0x800


ZI_CALLEES = {
    0x00622199: (2, "cdecl", zi_calloc_result),  # calloc
    0x00621B7E: (1, "cdecl", "probe"),           # free
}


def zi_huft_build(b, s, d, e, m, hn):
    """zlib 1.1.3's huft_build: (result, table index, m, hn, {entry index:
    (exop, bits, base)}), indexes counted in entries from the huft space."""
    n = len(b)
    c = [0] * 16
    for length in b:
        c[length] += 1
    if c[0] == n:
        return 0, None, 0, hn, {}
    j = 1
    while j <= 15 and c[j] == 0:
        j += 1
    k = j
    l = max(m, j)
    i = 15
    while i and c[i] == 0:
        i -= 1
    g = i
    l = min(l, i)
    m = l
    y = 1 << j
    while j < i:
        y -= c[j]
        if y < 0:
            return -3, None, m, hn, {}
        j += 1
        y <<= 1
    y -= c[i]
    if y < 0:
        return -3, None, m, hn, {}
    c[i] += y
    x = [0] * 16
    j = 0
    for length in range(1, i):
        j += c[length]
        x[length + 1] = j
    v = [0] * n
    for value in range(n):
        if b[value]:
            v[x[b[value]]] = value
            x[b[value]] += 1
    n = x[g]
    x[0] = i = 0
    p, h, w = 0, -1, -l
    u = [0] * 15
    q = z = 0
    t = None
    entries = {}
    r = [0, 0, 0]
    for k in range(k, g + 1):
        a = c[k]
        while a:
            a -= 1
            while k > w + l:
                h += 1
                w += l
                z = min(g - w, l)
                j = k - w
                f = 1 << j
                if f > a + 1:
                    f -= a + 1
                    xp = k
                    if j < z:
                        j += 1
                        while j < z:
                            f <<= 1
                            xp += 1
                            if f <= c[xp]:
                                break
                            f -= c[xp]
                            j += 1
                z = 1 << j
                if hn + z > 1440:
                    return -4, None, m, hn, entries
                q = u[h] = hn
                hn += z
                if h:
                    x[h] = i
                    r[1], r[0] = l, j
                    j = i >> (w - l)
                    r[2] = (q - u[h - 1] - j) & 0xFFFFFFFF
                    entries[u[h - 1] + j] = tuple(r)
                else:
                    t = q
            r[1] = k - w
            if p >= n:
                r[0] = 192
            elif v[p] < s:
                r[0] = 0 if v[p] < 256 else 96
                r[2] = v[p]
                p += 1
            else:
                r[0] = (e[v[p] - s] + 80) & 0xFF
                r[2] = d[v[p] - s]
                p += 1
            f = 1 << (k - w)
            j = i >> w
            while j < z:
                entries[q + j] = tuple(r)
                j += f
            j = 1 << (k - 1)
            while i & j:
                i ^= j
                j >>= 1
            i ^= j
            while (i & ((1 << w) - 1)) != x[h]:
                h -= 1
                w -= l
    return (-5 if y != 0 and g != 1 else 0), t, m, hn, entries


def zi_huffman_lengths(freqs, max_bits):
    """Code lengths of at most max_bits for these frequencies (0: unused)."""
    import heapq
    while True:
        used = [k for k, f in enumerate(freqs) if f]
        lengths = [0] * len(freqs)
        if len(used) <= 1:
            for k in used:
                lengths[k] = 1
            return lengths
        heap = [(freqs[k], k, (k,)) for k in used]
        heapq.heapify(heap)
        serial = len(freqs)
        while len(heap) > 1:
            fa, _, a = heapq.heappop(heap)
            fb, _, b = heapq.heappop(heap)
            for k in a + b:
                lengths[k] += 1
            heapq.heappush(heap, (fa + fb, serial, a + b))
            serial += 1
        if max(lengths) <= max_bits:
            return lengths
        freqs = [(f >> 1) + 1 if f else 0 for f in freqs]


def zi_lengths(rng, n, max_bits):
    """Valid (sometimes with one length changed), random, empty or single."""
    kind = rng.randrange(6)
    if kind <= 2:
        lengths = zi_huffman_lengths([0 if rng.random() < 0.25 else rng.randint(1, rng.choice((4, 5000)))
                                      for _ in range(n)], max_bits)
        if rng.random() < 0.3:
            lengths[rng.randrange(n)] = rng.randint(0, max_bits)
        return lengths
    if kind == 3:
        return [0 if rng.random() < 0.3 else rng.randint(0, max_bits) for _ in range(n)]
    lengths = [0] * n
    if kind == 5:
        lengths[rng.randrange(n)] = rng.randint(1, max_bits)
    return lengths


def zi_table(rng, presets, kind, hn, bits):
    """A complete decoding table in the huft space from hn on: (index, bits
    used, new hn) for kind "lit" (literal/length), "dist" or "bits" (the code
    length code); its entries are appended to presets."""
    for _ in range(20):
        if kind == "lit":
            n = rng.randint(257, 286)
            lengths = zi_huffman_lengths([rng.randint(0, 50) if k != 256 else 1 for k in range(n)], 15)
            result = zi_huft_build(lengths, 257, ZI_CPLENS_V, ZI_CPLEXT_V, bits, hn)
        elif kind == "dist":
            lengths = zi_huffman_lengths([rng.randint(0, 50) for _ in range(rng.randint(1, 30))], 15)
            result = zi_huft_build(lengths, 0, ZI_CPDIST_V, ZI_CPDEXT_V, bits, hn)
        else:
            lengths = zi_huffman_lengths([rng.randint(0, 30) for _ in range(19)], 7)
            result = zi_huft_build(lengths, 19, (), (), bits, hn)
        code, t, m, new_hn, entries = result
        if code in (0, -5) and t is not None:
            for index, (exop, nbits, base) in entries.items():
                presets.append((ZI_HUFTS + 8 * index, exop | nbits << 8 | base << 32, 8))
            return t, m, new_hn
    raise RuntimeError("no table")


def zi_data(rng):
    size = rng.choice((0, rng.randrange(40), rng.randrange(600), rng.randrange(3000)))
    out = bytearray()
    alphabet = rng.choice((256, rng.randint(2, 20)))
    while len(out) < size:
        kind = rng.randrange(4)
        if kind == 0:
            out += bytes(rng.randrange(alphabet) for _ in range(rng.randrange(64)))
        elif kind == 1:
            out += bytes([rng.randrange(alphabet)]) * rng.randrange(400)
        elif out:
            back = rng.randint(1, len(out))
            for _ in range(rng.randrange(300)):
                out.append(out[-back])
    return bytes(out[:size])


def zi_deflate(rng, data, wbits, raw):
    """Python's zlib (any level and strategy, flushes between parts)."""
    strategy = rng.choice((_zlib.Z_DEFAULT_STRATEGY, _zlib.Z_FILTERED, _zlib.Z_HUFFMAN_ONLY, _zlib.Z_RLE,
                           _zlib.Z_FIXED))
    co = _zlib.compressobj(rng.choice((0, 1, 6, 9)), _zlib.DEFLATED, -wbits if raw else wbits, 9, strategy)
    out = b""
    at = 0
    while at < len(data):
        part = rng.randint(1, max(1, len(data) - at)) if rng.random() < 0.5 else len(data) - at
        out += co.compress(data[at:at + part])
        at += part
        if at < len(data):
            out += co.flush(rng.choice((_zlib.Z_NO_FLUSH, _zlib.Z_SYNC_FLUSH, _zlib.Z_FULL_FLUSH)))
    return out + co.flush()


def zi_corrupt(rng, stream):
    stream = bytearray(stream)
    if stream and rng.random() < 0.25:
        for _ in range(rng.randint(1, 3)):
            stream[rng.randrange(len(stream))] ^= 1 << rng.randrange(8)
    if stream and rng.random() < 0.1:
        del stream[rng.randrange(len(stream)):]
    if rng.random() < 0.05:
        stream = bytearray(rng.getrandbits(8) for _ in range(rng.randrange(200)))
    return bytes(stream)


def zi_bytes(offset, data):
    return [(offset, int.from_bytes(data, "little"), len(data))] if data else []


def zi_window(rng, presets, room_min=0):
    """The blocks state's window: size, read and write pointers (wrapped or
    not, sometimes full); returns the window size."""
    size = rng.choice((0x8000, 0x8000, 0x200, rng.randint(0x400, 0x8000)))
    for _ in range(50):
        read = rng.choice((0, 0, size, rng.randrange(size + 1)))
        write = rng.choice((read, rng.randrange(size + 1), size, max(read - 1, 0)))
        room = read - write - 1 if write < read else size - write
        if room >= room_min:
            break
    else:
        read, write = 0, 0
    presets += [(ZI_BLOCKS + 0x28, WORLD_BASE + ZI_WINDOW, 4), (ZI_BLOCKS + 0x2c, WORLD_BASE + ZI_WINDOW + size, 4),
                (ZI_BLOCKS + 0x30, WORLD_BASE + ZI_WINDOW + read, 4),
                (ZI_BLOCKS + 0x34, WORLD_BASE + ZI_WINDOW + write, 4)]
    return size


def zi_stream(rng, presets, data, out_room=None):
    """The z_stream with this input, the output area and the allocators."""
    if out_room is None:
        out_room = rng.choice((0, 1, 7, 300, 5000, rng.randrange(4000)))
    presets += [(ZI_Z + 0x00, WORLD_BASE + ZI_IN, 4), (ZI_Z + 0x04, len(data), 4),
                (ZI_Z + 0x08, rng.getrandbits(32), 4), (ZI_Z + 0x0c, WORLD_BASE + ZI_OUT, 4),
                (ZI_Z + 0x10, out_room, 4), (ZI_Z + 0x14, rng.getrandbits(32), 4),
                (ZI_Z + 0x1c, WORLD_BASE + ZI_STATE, 4), (ZI_Z + 0x20, ZL_ZCALLOC, 4), (ZI_Z + 0x24, ZL_ZCFREE, 4),
                (ZI_Z + 0x28, 0, 4)]
    presets += zi_bytes(ZI_IN, data)


def zi_blocks_state(rng, presets, mode, bits=0, check=True):
    presets += [(ZI_BLOCKS + 0x00, mode, 4), (ZI_BLOCKS + 0x18, rng.getrandbits(1), 4),
                (ZI_BLOCKS + 0x1c, bits, 4), (ZI_BLOCKS + 0x20, rng.getrandbits(32) & ((1 << bits) - 1), 4),
                (ZI_BLOCKS + 0x24, WORLD_BASE + ZI_HUFTS, 4),
                (ZI_BLOCKS + 0x38, ZL_ADLER32 if check and rng.random() < 0.7 else 0, 4),
                (ZI_BLOCKS + 0x3c, rng.getrandbits(32), 4)]


def zi_args(make_case, *fields):
    """Generators: the first draws the case, the others read its fields."""
    def first(rng, pool):
        _ZI.clear()
        _ZI.update(make_case(rng))
        return _ZI[fields[0]]
    return [first] + [lambda rng, pool, f=f: _ZI[f] for f in fields[1:]]


def zi_presets(rng, pool, args):
    return list(_ZI["presets"])


# huft_build / inflate_trees_bits / inflate_trees_dynamic.

def zi_build_case(rng):
    presets = []
    kind = rng.randrange(3)
    if kind == 0:
        n, s, d, e, max_bits = 19, 19, 0, 0, 7
    elif kind == 1:
        n, s, d, e, max_bits = rng.randint(257, 288), 257, ZI_CPLENS, ZI_CPLEXT, 15
    else:
        n, s, d, e, max_bits = rng.randint(1, 30), 0, ZI_CPDIST, ZI_CPDEXT, 15
    lengths = zi_lengths(rng, n, max_bits)
    presets += [(ZI_LENS + 4 * k, length, 4) for k, length in enumerate(lengths)]
    presets += [(ZI_OUTS + 4, rng.choice((9, 6, 7, rng.randrange(16))), 4),
                (ZI_OUTS + 8, rng.choice((0, rng.randrange(40), 1440 - rng.randrange(600))), 4)]
    return {"b": WORLD_BASE + ZI_LENS, "n": n, "s": s, "d": d, "e": e, "t": WORLD_BASE + ZI_OUTS,
            "m": WORLD_BASE + ZI_OUTS + 4, "hp": WORLD_BASE + ZI_HUFTS, "hn": WORLD_BASE + ZI_OUTS + 8,
            "v": WORLD_BASE + ZI_V, "presets": presets}


def zi_trees_case(rng, bits=False):
    """inflate_trees_bits' 19 lengths or inflate_trees_dynamic's nl + nd (at
    most 15: larger ones would overrun huft_build's count table)."""
    presets = []
    nl, nd = rng.randint(257, 286), rng.randint(1, 30)
    if bits:
        lengths = zi_lengths(rng, 19, 7)
    else:
        lengths = zi_lengths(rng, nl, 15) + zi_lengths(rng, nd, 15)
    presets += [(ZI_LENS + 4 * k, length, 4) for k, length in enumerate(lengths)]
    presets += [(ZI_OUTS, rng.choice((7, 9, rng.randrange(16))), 4), (ZI_OUTS + 4, 6, 4)]
    zi_stream(rng, presets, b"")
    return {"nl": nl, "nd": nd, "c": WORLD_BASE + ZI_LENS, "o0": WORLD_BASE + ZI_OUTS, "o1": WORLD_BASE + ZI_OUTS + 4,
            "o2": WORLD_BASE + ZI_OUTS + 8, "o3": WORLD_BASE + ZI_OUTS + 12, "hp": WORLD_BASE + ZI_HUFTS,
            "z": WORLD_BASE + ZI_Z, "presets": presets}


# inflate_flush, inflate_fast, inflate_codes.

def zi_flush_case(rng):
    presets = []
    zi_window(rng, presets)
    zi_blocks_state(rng, presets, 0)
    zi_stream(rng, presets, b"", rng.choice((0, 1, 100, 0x10000, rng.randrange(0x9000))))
    return {"s": WORLD_BASE + ZI_BLOCKS, "z": WORLD_BASE + ZI_Z,
            "r": rng.choice((0, 1, -5 & 0xFFFFFFFF, -3 & 0xFFFFFFFF)), "presets": presets}


def zi_code_tables(rng, presets):
    """Fixed tables or dynamic ones (small lookup widths: many sub-tables)."""
    if rng.random() < 0.35:
        return 9, 5, ZI_FIXED_TL, ZI_FIXED_TD
    tl, bl, hn = zi_table(rng, presets, "lit", 0, rng.choice((9, rng.randint(1, 9))))
    td, bd, hn = zi_table(rng, presets, "dist", hn, rng.choice((6, rng.randint(1, 6))))
    return bl, bd, WORLD_BASE + ZI_HUFTS + 8 * tl, WORLD_BASE + ZI_HUFTS + 8 * td


def zi_codes_input(rng):
    if rng.random() < 0.5:  # deflate data (decoded with whatever tables)
        return zi_deflate(rng, zi_data(rng), 15, True)
    return bytes(rng.getrandbits(8) for _ in range(rng.choice((0, 3, 12, rng.randrange(800)))))


def zi_fast_case(rng):
    presets = []
    bl, bd, tl, td = zi_code_tables(rng, presets)
    zi_window(rng, presets, room_min=258)
    zi_blocks_state(rng, presets, 6, rng.randrange(8))
    data = zi_codes_input(rng)
    data += bytes(rng.getrandbits(8) for _ in range(max(0, 10 - len(data))))
    zi_stream(rng, presets, data)
    return {"bl": bl, "bd": bd, "tl": tl, "td": td, "s": WORLD_BASE + ZI_BLOCKS, "z": WORLD_BASE + ZI_Z,
            "presets": presets}


def zi_codes_case(rng):
    presets = []
    bl, bd, tl, td = zi_code_tables(rng, presets)
    size = zi_window(rng, presets)
    zi_blocks_state(rng, presets, 6, rng.choice((0, rng.randrange(8), rng.randrange(25))))
    zi_stream(rng, presets, zi_codes_input(rng))
    mode = rng.choice((0, 0, 0, 1, 2, 3, 4, 5, 5, 6, 7, 7, 8, 9, rng.randint(10, 0xFFFFFFFF)))
    sub08, sub0c, length = rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)
    if mode == 1:
        sub08, sub0c = tl, bl
    elif mode == 2:
        sub08, length = rng.randrange(6), rng.randint(3, 258)
    elif mode == 3:
        sub08, sub0c, length = td, bd, rng.randint(3, 258)
    elif mode == 4:
        sub08, sub0c, length = rng.randrange(14), rng.randint(1, size), rng.randint(3, 258)
    elif mode == 5:
        sub0c, length = rng.randint(1, size), rng.choice((0, rng.randint(1, 258)))
    presets += [(ZI_BLOCKS + 0x04, WORLD_BASE + ZI_CODES, 4),
                (ZI_CODES + 0x00, mode, 4), (ZI_CODES + 0x04, length, 4), (ZI_CODES + 0x08, sub08, 4),
                (ZI_CODES + 0x0c, sub0c, 4), (ZI_CODES + 0x10, bl, 1), (ZI_CODES + 0x11, bd, 1),
                (ZI_CODES + 0x14, tl, 4), (ZI_CODES + 0x18, td, 4)]
    return {"s": WORLD_BASE + ZI_BLOCKS, "z": WORLD_BASE + ZI_Z, "r": rng.choice((0, -5 & 0xFFFFFFFF)),
            "presets": presets}


# inflate_blocks, inflate.

def zi_blocks_setup(rng, presets, mode):
    """A blocks state in this mode with fitting sub-state; returns the input."""
    zi_window(rng, presets)
    if mode == 0:  # a fresh stream (or random bytes)
        zi_blocks_state(rng, presets, 0)
        presets.append((ZI_BLOCKS + 0x04, rng.getrandbits(32), 4))
        if rng.random() < 0.85:
            return zi_corrupt(rng, zi_deflate(rng, zi_data(rng), rng.randint(9, 15), True))
        return bytes(rng.getrandbits(8) for _ in range(rng.randrange(300)))
    data = bytes(rng.getrandbits(8) for _ in range(rng.choice((0, 1, 2, 5, rng.randrange(600)))))
    zi_blocks_state(rng, presets, mode, rng.choice((0, rng.randrange(8), rng.randrange(25))))
    word, index = rng.getrandbits(32), rng.getrandbits(32)
    if mode == 1 and rng.random() < 0.5:  # a stored block's lengths, right or wrong
        length = rng.randrange(0x400)
        complement = (~length & 0xFFFF) ^ (0 if rng.random() < 0.8 else 1 << rng.randrange(16))
        presets.append((ZI_BLOCKS + 0x1c, 0, 4))
        data = length.to_bytes(2, "little") + complement.to_bytes(2, "little") + data
    elif mode == 2:
        word = rng.choice((1, rng.randint(1, 0x400), rng.randint(1, 0xFFFF)))
    elif mode in (3, 4, 5):
        word = rng.randint(0, 29) | rng.randint(0, 29) << 5 | rng.randrange(16) << 10
        if rng.random() < 0.1:
            word = rng.randrange(0x4000)
        total = 258 + (word & 0x1F) + ((word >> 5) & 0x1F)
        presets.append((ZI_BLOCKS + 0x0c, WORLD_BASE + ZI_BLENS, 4))
        if mode == 4:
            index = rng.randint(0, 4 + (word >> 10))
        if mode == 5:
            index = rng.randrange(total)
            presets += [(ZI_BLENS + 4 * k, rng.randrange(16), 4) for k in range(total)]
            tb, bb, _ = zi_table(rng, presets, "bits", rng.choice((0, 300)), 7)
            presets += [(ZI_BLOCKS + 0x10, bb, 4), (ZI_BLOCKS + 0x14, WORLD_BASE + ZI_HUFTS + 8 * tb, 4)]
    if mode == 6:  # the codes state of a fixed block, at its start
        presets += [(ZI_BLOCKS + 0x04, WORLD_BASE + ZI_CODES, 4), (ZI_CODES + 0x00, 0, 4), (ZI_CODES + 0x10, 9, 1),
                    (ZI_CODES + 0x11, 5, 1), (ZI_CODES + 0x14, ZI_FIXED_TL, 4), (ZI_CODES + 0x18, ZI_FIXED_TD, 4)]
    else:
        presets.append((ZI_BLOCKS + 0x04, word, 4))
    presets.append((ZI_BLOCKS + 0x08, index, 4))
    return data


def zi_blocks_case(rng):
    presets = []
    mode = rng.choice((0,) * 8 + (1, 2, 3, 4, 5, 5, 6, 7, 8, 9, rng.randint(10, 0xFFFFFFFF)))
    data = zi_blocks_setup(rng, presets, mode)
    zi_stream(rng, presets, data, rng.choice((0, 1, 50, 0x10000, rng.randrange(5000))))
    return {"s": WORLD_BASE + ZI_BLOCKS, "z": WORLD_BASE + ZI_Z, "r": rng.choice((0, -5 & 0xFFFFFFFF)),
            "presets": presets}


def zi_inflate_case(rng):
    presets = []
    mode = rng.choice((0,) * 8 + (1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 11, 12, 13, rng.randint(14, 0xFFFFFFFF)))
    wbits = rng.randint(8, 15)
    nowrap = 0
    method = rng.choice((0x78, 0x08, rng.getrandbits(8), rng.getrandbits(32)))
    if mode == 0:  # a fresh zlib stream
        stream = bytearray(zi_deflate(rng, zi_data(rng), max(9, rng.randint(8, wbits)), False))
        if rng.random() < 0.05:  # a preset dictionary
            flags = (stream[1] & 0xC0) | 0x20
            stream[1] = flags | (31 - (stream[0] * 256 + flags) % 31) % 31
            stream[2:2] = bytes(rng.getrandbits(8) for _ in range(4))
        stream = zi_corrupt(rng, bytes(stream))
        zi_window(rng, presets)
        zi_blocks_state(rng, presets, 0)
    elif mode == 7:  # in the blocks of a stream, raw (nowrap) or not
        nowrap = rng.getrandbits(1)
        stream = zi_blocks_setup(rng, presets, rng.choice((0, 0, 0, 6, 8, 9)))
        if not nowrap:
            stream += bytes(rng.getrandbits(8) for _ in range(rng.randrange(6)))
    else:
        stream = bytes(rng.getrandbits(8) for _ in range(rng.choice((0, 1, 2, 4, 6))))
        if mode == 1:
            method = rng.choice((0x78, rng.getrandbits(8)))
            if stream and rng.random() < 0.6:  # a right header check
                flags = stream[0] & 0xE0
                stream = bytes([flags | (31 - (method * 256 + flags) % 31) % 31]) + stream[1:]
        zi_window(rng, presets)
        zi_blocks_state(rng, presets, rng.choice((0, 8)))
    need = rng.getrandbits(32)
    was = rng.getrandbits(32)
    if 8 <= mode <= 11 and rng.random() < 0.6:  # the check that the trailer bytes complete
        count = 12 - mode
        tail = int.from_bytes(stream[:count].ljust(count, b"\0"), "big")
        was = ((need if mode > 8 else 0) + tail) & 0xFFFFFFFF
    zi_stream(rng, presets, stream)
    presets += [(ZI_STATE + 0x00, mode, 4), (ZI_STATE + 0x04, was if mode >= 8 else method, 4),
                (ZI_STATE + 0x08, need, 4), (ZI_STATE + 0x0c, nowrap, 4),
                (ZI_STATE + 0x10, wbits, 4), (ZI_STATE + 0x14, WORLD_BASE + ZI_BLOCKS, 4)]
    z = WORLD_BASE + ZI_Z
    if rng.random() < 0.04:
        z = 0
    elif rng.random() < 0.04:
        presets.append((ZI_Z + 0x1c, 0, 4))  # no state
    elif rng.random() < 0.04:
        presets.append((ZI_Z + 0x00, 0, 4))  # no input pointer
    return {"z": z, "f": rng.choice((0, 4, 4, 2, rng.getrandbits(32))), "presets": presets}


def zi_spec(name, address, make_case, fields, **kwargs):
    return Spec(name, address, "stdcall", zi_args(make_case, *fields), presets=zi_presets, callees=ZI_CALLEES,
                ignore_bytes=ZI_HUFT_PADDING, **kwargs)


SPECS += [
    zi_spec("zlib_huft_build", 0x005b2410, zi_build_case, ("b", "n", "s", "d", "e", "t", "m", "hp", "hn", "v"),
            max_cases=1500),
    zi_spec("zlib_inflate_trees_bits", 0x005b2360, lambda rng: zi_trees_case(rng, bits=True), ("c", "o0", "o1", "hp", "z"), max_cases=800),
    zi_spec("zlib_inflate_trees_dynamic", 0x005b28e0, zi_trees_case,
            ("nl", "nd", "c", "o0", "o1", "o2", "o3", "hp", "z"), max_cases=1500),
    zi_spec("zlib_inflate_flush", 0x005b2aa0, zi_flush_case, ("s", "z", "r"), max_cases=600),
    zi_spec("zlib_inflate_fast", 0x005b2be0, zi_fast_case, ("bl", "bd", "tl", "td", "s", "z"), max_cases=600),
    # The exempt blocks are the "end - q" half of WAVAIL right after WRAP
    # moved q to the start of the window (NEEDOUT, inlined four / two
    # times): dead, WRAP only does that when read != window, and read never
    # lies before the window, so q < read there.
    zi_spec("zlib_inflate_codes", 0x005b1b90, zi_codes_case, ("s", "z", "r"), max_cases=1500,
            exempt_blocks=(0x005b1fa6, 0x005b2001, 0x005b2093, 0x005b20ee)),
    zi_spec("zlib_inflate_blocks", 0x005b0660, zi_blocks_case, ("s", "z", "r"), max_cases=1500,
            exempt_blocks=(0x005b085b, 0x005b08c0)),
    zi_spec("zlib_inflate", 0x005aa4a0, zi_inflate_case, ("z", "f"), max_cases=1500),
]


# -- png_rutil_rows.cpp --------------------------------------------------------
#
# Rows in the middle of the TP_ROW area, pixel depths PNG has (1..64 bits),
# rowbytes at least one pixel (as libpng's rows always are; fewer would make
# the average and Paeth filters loop over 4 GB in both).

PR_DEPTHS = (1, 2, 4, 8, 16, 24, 32, 48, 64)
PR_MASKS = (0xFF, 0x80, 0x08, 0x88, 0x22, 0xAA, 0x55, 0x33, 0xCC, 0x0F)


def pr_filter_presets(rng, pool, args):
    depth = rng.choice(PR_DEPTHS)
    bpp = (depth + 7) // 8
    width = rng.choice((0, 1, 2, 3, rng.randint(1, 150)))
    rowbytes = max(bpp, (width * depth + 7) // 8)
    return tp_png_struct(rng) + [(TP_ROW_INFO + 4, rowbytes, 4), (TP_ROW_INFO + 11, depth, 1)]


def pr_combine_presets(rng, pool, args):
    return tp_png_struct(rng) + [
        (TP_PNG + 0xc0, rng.choice((0, 1, 7, 8, 9, rng.randint(1, 150))), 4),
        (TP_PNG + 0x103, rng.choice(PR_DEPTHS), 1),
        (TP_PNG + 0x68, rng.getrandbits(32) & ~0x10000 | rng.choice((0, 0x10000)), 4),
        (TP_PNG + 0xe4, WORLD_BASE + TP_TABLES, 4),
    ]


def pr_interlace_presets(rng, pool, args):
    return [(TP_ROW_INFO, rng.choice((0, 1, 2, 3, 7, 8, 9, rng.randint(1, 60))), 4),
            (TP_ROW_INFO + 11, rng.choice(PR_DEPTHS), 1)]


SPECS += [
    tp_spec("png_read_filter_row", 0x005afc40,
            [TP_PNG_ARG, one_of(WORLD_BASE + TP_ROW_INFO), TP_ROW_ARG, one_of(TP_TABLES_PTR),
             lambda rng, pool: rng.choice((0, 1, 2, 3, 4, 4, 3, rng.getrandbits(32)))],
            presets=pr_filter_presets, eax_mask=0, max_cases=1000),
    tp_spec("png_combine_row", 0x005af4d0,
            [TP_PNG_ARG, TP_ROW_ARG, lambda rng, pool: rng.choice(PR_MASKS + (rng.getrandbits(32),))],
            presets=pr_combine_presets, eax_mask=0, max_cases=1000),
    tp_spec("png_do_read_interlace", 0x005af7f0,
            [one_of(0, WORLD_BASE + TP_ROW_INFO, WORLD_BASE + TP_ROW_INFO, WORLD_BASE + TP_ROW_INFO),
             one_of(0, TP_ROW_PTR, TP_ROW_PTR, TP_ROW_PTR), below(7),
             lambda rng, pool: rng.getrandbits(32) & ~0x10000 | rng.choice((0, 0x10000))],
            presets=pr_interlace_presets, eax_mask=0, max_cases=1000),
]


# -- png_rutil_chunks.cpp --------------------------------------------------------
#
# A chunk handler reads its data through the read callback: here its effect
# copies the next bytes of a stream in the world (PH_STREAM, cursor at
# PH_CURSOR): the chunk's data (made for the handler, sometimes of the wrong
# length or with bad values), its CRC (right 70 % of the time: png->crc is
# preset to the CRC of the chunk name, as png_read_info leaves it) and
# random bytes. Palettes and buffers in scratch; png_malloc's malloc probed.

import zlib as _ph_zlib

PH_STREAM = SCRATCH + 0x30000
PH_CURSOR = SCRATCH + 0x2FFF0


def ph_read(machine, ecx, args, h):
    data, length = args[1], args[2]
    cursor = int.from_bytes(machine.guest_read(WORLD_BASE + PH_CURSOR, 4), "little")
    length = min(length, 0x4000)
    if length:
        machine.guest_write(data, machine.guest_read(WORLD_BASE + PH_STREAM + cursor, length))
    machine.guest_write(WORLD_BASE + PH_CURSOR, (cursor + length).to_bytes(4, "little"))
    return None


PH_CALLEES = dict(TP_CALLEES)
PH_CALLEES[TP_READ_CB] = (3, "cdecl", "probe", ph_read, lambda m, ecx, a: (a[0], a[2]))


def ph_colour(rng):
    """(colour type, bit depth, channels): consistent mostly."""
    ct = rng.choice((0, 2, 3, 4, 6, 0, 2, 3, rng.randrange(8)))
    depth = rng.choice({0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}.get(ct, (8,)))
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ct, rng.randint(1, 4))
    return ct, depth, channels


def ph_case(rng, name, data_fn):
    """One handler call: {"png", "info", "length", "presets"}."""
    presets = tp_png_struct(rng)
    ct, depth, channels = ph_colour(rng)
    mode = rng.choice((0, 1, 3, 5, 7, 1, 3, 1, rng.getrandbits(5)))
    num_palette = rng.choice((0, 1, 16, 256, rng.randrange(300)))
    data = data_fn(rng, ct, depth, channels, num_palette)
    length = len(data) if rng.random() < 0.8 else rng.choice((0, 1, len(data) + 1, max(len(data) - 1, 0),
                                                              rng.randrange(40)))
    body = (data + bytes(max(0, length - len(data))))[:length]
    stream = body
    if rng.random() < 0.5:
        stream += _ph_zlib.crc32(name + body).to_bytes(4, "big")
    stream += bytes(rng.getrandbits(8) for _ in range(16))
    presets += [(TP_PNG + 0x60, mode, 4), (TP_PNG + 0x11e, ct, 1), (TP_PNG + 0x11f, depth, 1),
                (TP_PNG + 0x122, channels, 1), (TP_PNG + 0x110, num_palette, 2),
                (TP_PNG + 0x10c, WORLD_BASE + TP_TABLES, 4),
                (TP_PNG + 0x108, _ph_zlib.crc32(name), 4),
                (TP_INFO + 0x08, rng.getrandbits(32) if rng.random() < 0.3 else 0, 4),
                (PH_CURSOR, 0, 4), (PH_STREAM, int.from_bytes(stream, "little"), len(stream))]
    if rng.random() < 0.8:  # the real name, or with the ancillary bit flipped
        presets.append((TP_PNG + 0x114, int.from_bytes(name, "little") ^ (0x20 if rng.random() < 0.4 else 0), 4))
    info = 0 if rng.random() < 0.05 else TP_INFO_PTR
    return {"png": TP_PNG_PTR, "info": info, "length": length, "presets": presets}


def ph_spec(name, address, chunk, data_fn, **kwargs):
    kwargs.setdefault("max_cases", 6000)
    kwargs.setdefault("case_scale", 3)
    return Spec(name, address, "stdcall", zi_args(lambda rng: ph_case(rng, chunk, data_fn), "png", "info", "length"),
                presets=zi_presets, callees=PH_CALLEES, eax_mask=0, **kwargs)


def ph_bytes(rng, n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def ph_ihdr(rng, ct, depth, channels, num_palette):
    """A valid header, now and then with one field wrong."""
    ct = rng.choice((0, 2, 3, 4, 6))
    depth = rng.choice({0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8, 16), 4: (8, 16), 6: (8, 16)}[ct])
    fields = [rng.choice((1, 16, 300, rng.randint(1, 0x7FFFFFFF))), rng.choice((1, 16, 300, rng.randint(1, 0x7FFFFFFF))),
              depth, ct, 0, 0, rng.choice((0, 1))]
    if rng.random() < 0.4:
        k = rng.choice((0, 1, 2, 2, 2, 3, 3, 4, 5, 6))
        fields[k] = rng.choice(((0, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF), (0, 0x80000000, 0x7FFFFFFF),
                                (0, 3, 5, 32, 7, 1, 2, 4), (1, 5, 7, 8, 255, 2, 4, 6),
                                (1, 255), (1, 2), (2, 3, 255))[k])
    return (fields[0].to_bytes(4, "big") + fields[1].to_bytes(4, "big") + bytes(fields[2:]))


def ph_sbit(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 3 if ct == 3 else channels)


def ph_trns(rng, ct, depth, channels, num_palette):
    if ct == 3:
        return ph_bytes(rng, rng.choice((0, 1, num_palette, min(num_palette, 256), rng.randrange(300))))
    return ph_bytes(rng, 6 if ct == 2 else 2)


def ph_bkgd(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 1 if ct == 3 else 6 if ct & 2 else 2)


def ph_hist(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 2 * num_palette)


def ph_nine(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 9)


def ph_time(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 7)


def ph_iend(rng, ct, depth, channels, num_palette):
    return b"" if rng.random() < 0.8 else ph_bytes(rng, rng.randint(1, 30))


SPECS += [
    ph_spec("png_handle_IHDR", 0x005aab20, b"IHDR", ph_ihdr),
    ph_spec("png_handle_IEND", 0x005ab300, b"IEND", ph_iend),
    ph_spec("png_handle_sBIT", 0x005ab8b0, b"sBIT", ph_sbit),
    ph_spec("png_handle_tRNS", 0x005ace60, b"tRNS", ph_trns),
    ph_spec("png_handle_bKGD", 0x005ad3b0, b"bKGD", ph_bkgd),
    ph_spec("png_handle_hIST", 0x005ad860, b"hIST", ph_hist),
    ph_spec("png_handle_pHYs", 0x005adcb0, b"pHYs", ph_nine),
    ph_spec("png_handle_oFFs", 0x005ae070, b"oFFs", ph_nine),
    # info+0x43: the padding byte of tIME's png_time, stack garbage in the
    # original (see png_rutil_chunks.cpp); its neighbours (second at +0x42,
    # the year at +0x3c) are compared.
    ph_spec("png_handle_tIME", 0x005ae970, b"tIME", ph_time, ignore_bytes=(TP_INFO + 0x43,)),
]


def ph_text(rng, ct, depth, channels, num_palette):
    key = bytes(0x61 + rng.randrange(26) for _ in range(rng.randrange(12)))
    sep = b"" if rng.random() < 0.15 else b"\0"
    text = bytes(0 if rng.random() < 0.05 else 32 + rng.randrange(90) for _ in range(rng.randrange(60)))
    return key + sep + text


def ph_unknown_case(rng):
    case = ph_case(rng, b"abCd", lambda rng, *rest: ph_bytes(rng, rng.randrange(40)))
    name = bytes(rng.getrandbits(8) if rng.random() < 0.1 else rng.choice((0x41, 0x61)) + rng.randrange(26)
                 for _ in range(4))
    case["presets"].append((TP_PNG + 0x114, int.from_bytes(name, "little"), 4))
    return case


def ph_text_info(rng):
    """png_set_text's info: no text array or one with room for 0..2 more."""
    num = rng.randrange(4)
    if rng.random() < 0.33:
        # (no room: with room, png_set_text would write through the null pointer)
        return [(TP_INFO + 0x30, num, 4), (TP_INFO + 0x34, rng.choice((0, -rng.randrange(8) & 0xFFFFFFFF)), 4),
                (TP_INFO + 0x38, 0, 4)]
    return [(TP_INFO + 0x30, num, 4), (TP_INFO + 0x34, num + rng.randrange(3), 4),
            (TP_INFO + 0x38, WORLD_BASE + TP_TABLES + 0x800, 4)]


def ph_text_case(rng):
    case = ph_case(rng, b"tEXt", ph_text)
    case["presets"] += ph_text_info(rng)
    return case


PH_ENTRIES = SCRATCH + 0x3C000
PH_STRINGS = SCRATCH + 0x3D000


def ph_set_text_case(rng):
    presets = tp_png_struct(rng) + ph_text_info(rng)
    count = rng.choice((0, 1, 2, 3, 1))
    for k in range(count):
        string = PH_STRINGS + 0x100 * k
        text = rng.choice((0, WORLD_BASE + string, WORLD_BASE + string, WORLD_BASE + string + rng.randrange(40)))
        presets += [(PH_ENTRIES + 16 * k, rng.choice((0xFFFFFFFF, 0, 1)), 4),
                    (PH_ENTRIES + 16 * k + 4, WORLD_BASE + string + 0x80, 4), (PH_ENTRIES + 16 * k + 8, text, 4),
                    (string + rng.randrange(40), 0, 1)]
        if text and rng.random() < 0.25:
            presets.append((text - WORLD_BASE, 0, 1))
    return {"png": 0 if rng.random() < 0.05 else TP_PNG_PTR, "info": 0 if rng.random() < 0.05 else TP_INFO_PTR,
            "text": WORLD_BASE + PH_ENTRIES, "count": count, "presets": presets}


SPECS += [
    Spec("png_handle_tEXt", 0x005aed30, "stdcall", zi_args(ph_text_case, "png", "info", "length"),
         presets=zi_presets, callees=PH_CALLEES, eax_mask=0, max_cases=6000, case_scale=3),
    # The exempt blocks: the critical-chunk halves of the inlined CRC check,
    # reached only after png_chunk_error("unknown critical chunk") returned,
    # which it never does.
    Spec("png_handle_unknown", 0x005af300, "stdcall", zi_args(ph_unknown_case, "png", "info", "length"),
         presets=zi_presets, callees=PH_CALLEES, eax_mask=0, max_cases=6000, case_scale=3,
         exempt_blocks=(0x005af41a, 0x005af495)),
    Spec("png_set_text", 0x005b19a0, "stdcall", zi_args(ph_set_text_case, "png", "info", "text", "count"),
         presets=zi_presets, callees=PH_CALLEES, eax_mask=0, max_cases=2000),
]


def ph_plte(rng, ct, depth, channels, num_palette):
    return ph_bytes(rng, 3 * rng.randrange(260) if rng.random() < 0.8 else rng.randrange(40))


def ph_plte_case(rng):
    case = ph_case(rng, b"PLTE", ph_plte)
    case["presets"].append((TP_PNG + 0x112, rng.choice((0, 1, 16, 300, rng.randrange(400))), 2))
    return case


SPECS += [
    Spec("png_handle_PLTE", 0x005aaea0, "stdcall", zi_args(ph_plte_case, "png", "info", "length"),
         presets=zi_presets, callees=PH_CALLEES, eax_mask=0, max_cases=6000, case_scale=3),
]


# -- png_read_destroy.cpp ----------------------------------------------------------
#
# The png_struct at TP_PNG and two infos (TP_INFO, PD_END_INFO); everything
# they free is null or a block in scratch (free probed: the order of the
# frees is compared); 16-bit gamma tables of 1 << (8 - gamma_shift) rows; an
# inflate stream without state (inflateEnd returns at once).

PD_END_INFO = 0x1200
PD_AREA = SCRATCH + 0x40000
PD_SLOTS = SCRATCH + 0x4F000


def pd_block(rng, k):
    return 0 if rng.random() < 0.25 else WORLD_BASE + PD_AREA + 0x10 * (k % 0x100)


def pd_info(rng, base, area):
    presets = []
    num = rng.randrange(4)
    presets.append((base + 0x30, num, 4))
    if rng.random() < 0.66:
        presets.append((base + 0x38, WORLD_BASE + area, 4))  # the text array
        presets += [(area + 16 * i + 4, pd_block(rng, rng.getrandbits(8)), 4) for i in range(num)]
    else:
        presets.append((base + 0x38, 0, 4))
    nparams = rng.randrange(4)
    presets += [(base + 0xa0, pd_block(rng, rng.getrandbits(8)), 4), (base + 0xac, pd_block(rng, rng.getrandbits(8)), 4),
                (base + 0xb5, nparams, 1)]
    if rng.random() < 0.66:
        presets.append((base + 0xb0, WORLD_BASE + area + 0x100, 4))
        presets += [(area + 0x100 + 4 * i, pd_block(rng, rng.getrandbits(8)), 4) for i in range(nparams)]
    else:
        presets.append((base + 0xb0, 0, 4))
    return presets


def pd_presets(rng, pool, args):
    presets = [(TP_PNG + off, pd_block(rng, k), 4)
               for k, off in enumerate((0xa4, 0xe4, 0xe0, 0x1e4, 0x1e8, 0x15c, 0x160, 0x164, 0x208, 0x1a8, 0x10c,
                                        0x180, 0x1ec))]
    shift = rng.randrange(9)
    presets += [(TP_PNG + 0x64, rng.getrandbits(32), 4), (TP_PNG + 0x150, shift, 4), (TP_PNG + 0x6c + 0x1c, 0, 4)]
    for k in range(3):
        if rng.random() < 0.33:
            presets.append((TP_PNG + 0x168 + 4 * k, 0, 4))
            continue
        table = SCRATCH + 0x44000 + 0x400 * k
        presets.append((TP_PNG + 0x168 + 4 * k, WORLD_BASE + table, 4))
        presets += [(table + 4 * r, pd_block(rng, r + 40 * k), 4) for r in range(1 << (8 - shift))]
    presets += pd_info(rng, TP_INFO, SCRATCH + 0x48000) + pd_info(rng, PD_END_INFO, SCRATCH + 0x49000)
    presets += [(PD_SLOTS, TP_PNG_PTR, 4), (PD_SLOTS + 4, rng.choice((0, TP_INFO_PTR, TP_INFO_PTR)), 4),
                (PD_SLOTS + 8, rng.choice((0, WORLD_BASE + PD_END_INFO)), 4)]
    return presets


PD_INFO_ARG = one_of(0, TP_INFO_PTR, TP_INFO_PTR, TP_INFO_PTR)
PD_END_ARG = one_of(0, WORLD_BASE + PD_END_INFO)

SPECS += [
    tp_spec("png_info_destroy", 0x005a4f60, [TP_PNG_ARG, one_of(TP_INFO_PTR)], presets=pd_presets, eax_mask=0),
    tp_spec("png_read_destroy", 0x005a5b90, [TP_PNG_ARG, PD_INFO_ARG, PD_END_ARG], presets=pd_presets, eax_mask=0),
    tp_spec("png_destroy_read_struct", 0x005a5b00,
            [one_of(0, WORLD_BASE + PD_SLOTS, WORLD_BASE + PD_SLOTS), one_of(0, WORLD_BASE + PD_SLOTS + 4),
             one_of(0, WORLD_BASE + PD_SLOTS + 8)], presets=pd_presets, eax_mask=0),
]


def ph_gama(rng, ct, depth, channels, num_palette):
    value = rng.choice((45000, 45455, 0, rng.getrandbits(32) >> rng.randrange(32), 100000, 45454))
    return value.to_bytes(4, "big")


def ph_gama_case(rng):
    case = ph_case(rng, b"gAMA", ph_gama)
    case["presets"].append((TP_INFO + 0x08, rng.choice((0, 0x800, 0x801, 1, rng.getrandbits(32))), 4))
    return case


SPECS += [
    # (A null info_ptr crashes both once a value is read: invalid cases.)
    Spec("png_handle_gAMA", 0x005ab470, "stdcall", zi_args(ph_gama_case, "png", "info", "length"),
         presets=zi_presets, callees=PH_CALLEES, eax_mask=0, max_cases=6000, case_scale=3),
]
