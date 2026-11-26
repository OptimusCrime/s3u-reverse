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
