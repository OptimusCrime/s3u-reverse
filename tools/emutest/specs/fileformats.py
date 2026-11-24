"""Emulator specs, part `fileformats`: specs of the replacements in src/s3re/game/fileformats/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""

# --- session 51: CMapFile container (wip/mapfile) ---
# Dirk::CMapFile (map_file.cpp) with ECX = the world, so the object sits at
# world offset 0. The file is modelled in Python: the stream's Read
# (0x4a57b0) and Seek (0x4a5930) are probed, and their effects read a
# random container (derived from the case seed) at a position kept in the
# stream's CFile handle field. Reads into the caller's stack record "stack"
# instead of the address. The skip-list code of the chunk cache runs as is;
# its level draws get many bits left (g_skipListRandomBitsLeft), so the
# CRT's rand() (per-thread data, TlsGetValue) is never reached.
MF_MEMORY_ONLY = 0x04
MF_POSITION = 0x10  # the stream's CFile handle: here the fake file position
MF_ERROR = 0x18
MF_CHUNKS = 0x30
MF_REFERENCES = 0xb0
MF_NODES = SCRATCH + 0x2000
MF_BUFFERS = SCRATCH + 0x4000
MF_CHUNK_DICT_VTABLE = 0x658794  # WCValSkipListDict<unsigned int, void*>
MF_REFERENCE_DICT_VTABLE = 0x658778  # WCValSkipListDict<unsigned int, int>
MF_STREAM_READ = 0x004a57b0
MF_STREAM_SEEK = 0x004a5930


def _mf_key(rng):
    return ((rng.randrange(3) if rng.random() < 0.25 else 0) << 16) | rng.randrange(5)


def _mf_container(seed):
    """The fake file of a case: like the in-process test's RandomContainer."""
    rng = random.Random(seed * 7919 + 0x3A9)
    data = bytearray(struct.pack("<II", rng.getrandbits(32), rng.choice((10, 11))))
    for _ in range(rng.randrange(8)):
        size = 8 + rng.randrange(48)
        data += struct.pack("<II", _mf_key(rng), size) + bytes(rng.getrandbits(8) for _ in range(size - 8))
    if rng.random() < 0.25:
        size = 16 + rng.randrange(40)
        data += struct.pack("<II", _mf_key(rng), size) + bytes(rng.randrange(size - 8))
    elif rng.random() < 0.25:
        data += bytes(1 + rng.randrange(7))
    return bytes(data)


def _mf_dword(m, address):
    return struct.unpack("<I", m.guest_read(address, 4))[0]


def _mf_read(m, ecx, args, h):
    """CMapFileStream::Read(buffer, size) on the fake file."""
    buffer, size = args
    if _mf_dword(m, ecx + 0x10) != 0:
        return 0xFFFFFFFF  # an error is pending: the stream doesn't read
    if h % 40 == 0:
        m.guest_write(ecx + 0x10, struct.pack("<I", 0x80070057))  # a read error (a caught CHResult)
        return 0xFFFFFFFF
    data = _mf_container(m.seed)
    position = _mf_dword(m, ecx + 8)
    part = data[position:position + size] if position < len(data) else b""
    if part:
        m.guest_write(buffer, part)
    m.guest_write(ecx + 8, struct.pack("<I", position + len(part)))
    return len(part)


def _mf_seek(m, ecx, args, h):
    """CMapFileStream::Seek(offset, origin) on the fake file."""
    offset = struct.unpack("<i", struct.pack("<I", args[0]))[0]
    origin = args[1]
    position = _mf_dword(m, ecx + 8)
    if origin == 0:
        new = offset
    elif origin == 1:
        new = position + offset
    else:
        new = len(_mf_container(m.seed)) + offset
    new &= 0xFFFFFFFF
    m.guest_write(ecx + 8, struct.pack("<I", new))
    return new


def _mf_record_read(m, ecx, args):
    buffer, size = args
    return (buffer if WORLD_BASE <= buffer < WORLD_BASE + 0x2000000 else "stack", size)


MF_STREAM_PROBES = {
    MF_STREAM_READ: (2, "thiscall", "probe", _mf_read, _mf_record_read),
    MF_STREAM_SEEK: (2, "thiscall", "probe", _mf_seek),
}


def _mf_dict(offset, vtable, entries, node_base, rng):
    """A skip-list dictionary at world `offset` holding `entries` (key,
    value, level), with its nodes at `node_base`."""
    top = max((level for _, _, level in entries), default=0)
    fields = {offset: vtable, offset + 0x58: 0, offset + 0x5c: 0, offset + 0x60: 0, offset + 0x64: 8,
              offset + 0x68: 3, offset + 0x6c: top, offset + 0x70: WORLD_BASE + offset + 8,
              offset + 0x74: len(entries), offset + 0x78: rng.getrandbits(32), offset + 0x7c: rng.getrandbits(32)}
    links = {}  # level -> where the next node of that level is linked in
    for level in range(20):
        fields[offset + 8 + 4 * level] = 0
        links[level] = offset + 8 + 4 * level
    for i, (key, value, level) in enumerate(sorted(entries)):
        base = node_base + i * 0x20
        fields[base] = key
        fields[base + 4] = value
        for link in range(level + 1):
            fields[base + 8 + 4 * link] = 0
            fields[links[link]] = WORLD_BASE + base + 8
            links[link] = base + 8 + 4 * link
    return [(address, value, 4) for address, value in fields.items()]


def mapfile_cache(rng, key):
    """The chunk cache: 0..3 cached keys (the argument's key half of the
    time), sometimes a key only in the reference counts."""
    keys = set()
    for _ in range(rng.randrange(4)):
        keys.add(_mf_key(rng) if rng.random() < 0.7 else rng.getrandbits(32))
    if keys and rng.random() < 0.5:
        keys.add(key)
    chunk_entries, reference_entries = [], []
    for i, k in enumerate(sorted(keys)):
        level = 1 if rng.random() < 0.3 else 0
        reference_entries.append((k, rng.choice((1, 1, 2, 3)), level))
        if rng.random() < 0.9:
            chunk_entries.append((k, WORLD_BASE + MF_BUFFERS + i * 0x40, level))
    return (_mf_dict(MF_CHUNKS, MF_CHUNK_DICT_VTABLE, chunk_entries, MF_NODES, rng)
            + _mf_dict(MF_REFERENCES, MF_REFERENCE_DICT_VTABLE, reference_entries, MF_NODES + 0x200, rng))


def _mf_args_key(args):
    return ((args[1] & 0xFFFF) << 16) | (args[0] & 0xFFFF)


def mapfile_world(rng, pool, args):
    fields = [(MF_MEMORY_ONLY, 1 if rng.random() < 0.1 else 0, 1), (MF_POSITION, rng.randrange(64), 4),
              (MF_ERROR, 0 if rng.random() < 0.9 else rng.getrandbits(32) | 1, 4)]
    return fields + mapfile_cache(rng, _mf_args_key(args))


def mapfile_add_world(rng, pool, args):
    data = [(SCRATCH, rng.getrandbits(32), 4), (SCRATCH + 4, 8 + rng.randrange(40), 4)]
    return data + mapfile_cache(rng, _mf_args_key(args))


def mapfile_globals(rng, pool, args):
    return [(0x007a4030, 1000, 4), (0x007e8e24, rng.getrandbits(32), 4)]


def _mf_type(rng, pool):
    value = rng.randrange(5)
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.15 else 0)


def _mf_sub(rng, pool):
    value = rng.randrange(3) if rng.random() < 0.25 else 0
    return value | (rng.getrandbits(16) << 16 if rng.random() < 0.15 else 0)


SPECS += [
    Spec("CMapFileStream_HasError", 0x004a5a60, "thiscall", [], eax_mask=0xFF,
         presets=lambda rng, pool, args: [(0x10, 0 if rng.random() < 0.5 else rng.getrandbits(32), 4)]),
    Spec("CMapFile_SetMemoryOnly", 0x004a60c0, "thiscall", [], eax_mask=0),
    Spec("CMapFile_GetError", 0x004a60d0, "thiscall", [],
         presets=lambda rng, pool, args: [(MF_MEMORY_ONLY, rng.choice((0, 0, 1, rng.getrandbits(8))), 1)]),
    Spec("CMapFile_GetChecksum", 0x004a6a30, "thiscall", []),
    # Not reached: 0x4a6177 and 0x4a62e7, where a skip-list insert returned
    # null (only when operator new fails for the node; the original then
    # dereferences null); 0x4a6143, an insert after the second find of a
    # key the first find of the same call found (dead). The out-of-memory
    # path of the chunk buffer (ThrowOutOfMemory) is reached with new_fails;
    # both functions then fault inside ThrowOutOfMemory, whose own
    # allocations fail too (counted as invalid cases; the same in AddChunk).
    Spec("CMapFile_GetChunk", 0x004a60f0, "thiscall", [_mf_type, _mf_sub], presets=mapfile_world,
         globals=mapfile_globals, callees=MF_STREAM_PROBES, new_fails=0.03,
         exempt_blocks=(0x004a6143, 0x004a6177, 0x004a62e7)),
    # The skip-list node destructor (vtable slot 4, 0x4a8f10) is probed: it
    # calls the folded empty function at DebugLog's address (0x428720) with
    # the remove caller's EDI as the stack slot the harness reads as the
    # format, a register leftover. Not reached: 0x4a635a and 0x4a637f,
    # inserts after a key the same call already found in the reference
    # counts (dead).
    Spec("CMapFile_ReleaseChunk", 0x004a6310, "thiscall", [_mf_type, _mf_sub], presets=mapfile_world,
         globals=mapfile_globals, callees={0x004a8f10: (2, "thiscall", "probe")},
         exempt_blocks=(0x004a635a, 0x004a637f)),
    # Not reached: 0x4a64ee, a failed insert into the chunk dictionary (as
    # above); 0x4a64b9 and 0x4a64e2, inserts after finds of keys the same
    # call already found or inserted (dead).
    Spec("CMapFile_AddChunk", 0x004a63f0, "thiscall", [_mf_type, _mf_sub, one_of(WORLD_BASE + SCRATCH)],
         presets=mapfile_add_world, globals=mapfile_globals, new_fails=0.03,
         exempt_blocks=(0x004a64b9, 0x004a64e2, 0x004a64ee)),
]


# CMapFileStream (map_file_stream.cpp) with ECX = the world: the stream at
# world offset 0 (rethrow +0, CFile +4 with its vtable, handle +8, owned
# +0xc, error +0x10). The Win32::CFile calls are probed (they reach
# kernel32); CFile::Read/Write's bytes-transferred pointer is a stack
# local of the stream member (recorded as "stack"), and the probe's effect
# stores a transfer count there. Not reached: the catch blocks, which only
# a C++ exception thrown by CFile reaches (in-process MapFileStream_* tests
# reach them on real files).
STREAM_ERROR = 0x10
STREAM_PATH = SCRATCH  # a std::string: allocator, ptr, len, res


def _stream_transfer_effect(m, ecx, args, h):
    m.guest_write(args[2], struct.pack("<I", h % (args[1] + 1)))


def _stream_record_transfer(m, ecx, args):
    return (args[0], args[1], "stack", args[3])


def stream_world(rng, pool, args):
    return [(0, rng.choice((0, 0, 1, rng.getrandbits(8))), 1),
            (STREAM_ERROR, 0 if rng.random() < 0.7 else rng.getrandbits(32), 4)]


def stream_open_world(rng, pool, args):
    text = WORLD_BASE + SCRATCH + 0x40
    return stream_world(rng, pool, args) + [(STREAM_PATH + 4, 0 if rng.random() < 0.2 else text, 4),
                                            (STREAM_PATH + 8, rng.randrange(20), 4)]


def stream_close_world(rng, pool, args):
    return stream_world(rng, pool, args) + [(4, FAKE_VTABLE, 4)]


STREAM_OPEN_MODES = one_of(0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF, 0x10)

SPECS += [
    # The CFile constructor (0x6195c0) runs as is.
    Spec("CMapFileStream_Construct", 0x004a55e0, "thiscall", [anything]),
    Spec("CMapFileStream_Open", 0x004a5600, "thiscall", [one_of(WORLD_BASE + STREAM_PATH), STREAM_OPEN_MODES],
         presets=stream_open_world, callees={0x00619610: (7, "thiscall", "probe")},
         exempt_blocks=(0x004a56a3, 0x004a56b3, 0x004a56ba, 0x004a56c0, 0x004a56c6, 0x004a56e5,
                        0x004a56e9, 0x004a56f1)),
    Spec("CMapFileStream_Close", 0x004a5710, "thiscall", [], presets=stream_close_world, virtuals={4: 0},
         exempt_blocks=(0x004a576d, 0x004a5783, 0x004a5787, 0x004a578f)),
    Spec("CMapFileStream_Read", 0x004a57b0, "thiscall", [one_of(WORLD_BASE + SCRATCH), between(0, 0x400)],
         presets=stream_world,
         callees={0x006198d0: (4, "thiscall", "probe", _stream_transfer_effect, _stream_record_transfer)},
         exempt_blocks=(0x004a5817, 0x004a582d, 0x004a5831, 0x004a5839, 0x004a583f, 0x004a5844)),
    Spec("CMapFileStream_Write", 0x004a5870, "thiscall", [one_of(WORLD_BASE + SCRATCH), between(0, 0x400)],
         presets=stream_world,
         callees={0x00619b10: (4, "thiscall", "probe", _stream_transfer_effect, _stream_record_transfer)},
         exempt_blocks=(0x004a58d7, 0x004a58ed, 0x004a58f1, 0x004a58f9, 0x004a58ff, 0x004a5904)),
    Spec("CMapFileStream_Seek", 0x004a5930, "thiscall", [anything, below(4)], presets=stream_world,
         callees={0x00619d50: (3, "thiscall", "probe")},
         exempt_blocks=(0x004a5980, 0x004a5996, 0x004a599a, 0x004a59a2, 0x004a59a8)),
    # Coverage up to the end of the epilogue Ghidra split off as 0x4a5a37.
    Spec("CMapFileStream_GetSize", 0x004a59d0, "thiscall", [], presets=stream_world,
         callees={0x0061a080: (1, "thiscall", "probe")}, cover_until=0x004a5a60,
         exempt_blocks=(0x004a5a09, 0x004a5a1f, 0x004a5a23, 0x004a5a2b, 0x004a5a31)),
]
