# Specs of src/s3re/game/sound/ (session 56, wip/sound): CSoundData.
#
# The object sits at world 0 (CSoundData, 0x14c bytes). Its dictionaries
# (+0x40 effects, +0xc0 last use) are built by the presets as level-0
# chains (header at +0x48 / +0xc8, nodes in SND_NODES: [key][value][next],
# a node pointer points at its next field); the dictionaries' own code
# (clear 0x60bd80) is probed: it is the Watcom library, run unchanged by
# both. The effects' methods (~CSoundEffect 0x5c2b00, CSoundEffect::Restore
# 0x5c2d20) and Disconnect 0x5c1da0 (own spec in framework.py) are probed
# where the CSoundData code calls them. COM objects and the game objects'
# vtables as in framework.py (DS_* helpers): FAKE_VTABLE for interfaces,
# FAKE_VTABLE + DS_GAME_SLOTS for game objects.
#
# Not reachable in the emulator (a C++ exception ends the run): the catch
# blocks of Open (0x5fe8ec..0x5fe945, 0x5fe97f..0x5fe9d5, 0x5feace..0x5feb02)
# and Restore (0x5fdf90..0x5fdfb7); the in-process tests SoundData_* run
# them with scripted DirectSound failures.

SND_NODES = SCRATCH + 50 * SCRATCH_SLOT_BYTES     # dictionary nodes (0x10 apart)
SND_EFFECTS = SCRATCH + 51 * SCRATCH_SLOT_BYTES   # fake effect objects (0x20 apart)
SND_PRIMARY = SCRATCH + 52 * SCRATCH_SLOT_BYTES   # a fake primary buffer object
SND_MAPPINGS = SCRATCH + 53 * SCRATCH_SLOT_BYTES  # fake mapping objects (0x40 apart)
SND_HEADERS = SCRATCH + 54 * SCRATCH_SLOT_BYTES   # fake SND file headers (0x100 apart)
SND_OBJECT_BYTES = 0x14c

SND_PROBES = {
    0x0060bd80: (0, "thiscall", "probe"),  # WCSkipNonTempBase::clear (Watcom library)
    0x005c1da0: (0, "thiscall", "probe"),  # CDirectSound::Disconnect (own spec)
    0x005c2b00: (0, "thiscall", "probe"),  # ~CSoundEffect (not reimplemented)
    0x005c2d20: (0, "thiscall", "probe"),  # CSoundEffect::Restore (not reimplemented)
    0x0061aa40: (1, "thiscall", "probe"),  # CFileMapping::UnmapView (own spec)
}


def snd_dict(fields, offset, entries, rng):
    """A dictionary at world `offset` with `entries` (key, value) nodes on
    level 0 (header at offset + 8, count at +0x74)."""
    header = offset + 8
    fields += [(offset + 0x70, WORLD_BASE + header, 4), (offset + 0x74, len(entries), 4),
               (offset + 0x6c, 0, 4), (offset + 0x64, 8, 4), (offset + 0x60, 0, 4)]
    fields += [(header + 4 * k, 0, 4) for k in range(20)]
    previous = header
    for i, (key, value) in enumerate(entries):
        node = SND_NODES + 0x10 * (i + (0 if offset == 0x40 else 8))
        fields += [(node, key, 4), (node + 4, value, 4), (node + 8, 0, 4)]
        fields.append((previous, WORLD_BASE + node + 8, 4))
        previous = node + 8
    return fields


def snd_world(rng, pool, args):
    """The object: game vtable, a fake IDirectSound or none, enabled, a
    folder string, views and mappings, the dictionaries, the primary buffer
    (a fake game object with a fake IDirectSoundBuffer) or none."""
    fields = [(0, FAKE_VTABLE + DS_GAME_SLOTS, 4),
              (4, 0 if rng.random() < 0.3 else WORLD_BASE + DS_IFACE, 4),
              (0xC, rng.choice([0, 1, 1, rng.getrandbits(8)]), 1)]
    fields += [(DS_IFACE + 0x10 * k, FAKE_VTABLE, 4) for k in range(4)]
    fields += gfx_text_string(rng, 0x10, [b"C:\\S3\\Snd\\", b"", None])
    for i in range(2):
        mapped = rng.random() < (0.8 if i == 0 else 0.5)
        fields += [(0x30 + 4 * i, rng.getrandbits(32) | 1 if mapped else 0, 4),
                   (0x28 + 4 * i, 0 if rng.random() < 0.05 else WORLD_BASE + SND_MAPPINGS + 0x40 * i, 4),
                   (SND_MAPPINGS + 0x40 * i, FAKE_VTABLE, 4),
                   (0x20 + 4 * i, rng.getrandbits(32), 4)]
    effects = []
    for i in range(rng.choice([0, 1, 2, 4])):
        value = 0 if rng.random() < 0.15 else WORLD_BASE + SND_EFFECTS + 0x20 * i
        effects.append((rng.getrandbits(32), value))
    fields = snd_dict(fields, 0x40, sorted(effects), rng)
    uses = [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(rng.choice([0, 0, 1, 3]))]
    fields = snd_dict(fields, 0xC0, sorted(uses), rng)
    primary = 0 if rng.random() < 0.25 else WORLD_BASE + SND_PRIMARY
    fields += [(0x140, primary, 4), (SND_PRIMARY, FAKE_VTABLE + DS_GAME_SLOTS, 4),
               (SND_PRIMARY + 4, WORLD_BASE + DS_IFACE + 0x10, 4)]
    return fields


SND_SLOTS = {0x00: 1, 0x04: 0,                                    # mappings: delete, Close
             DS_GAME_SLOTS + 0x00: 1, DS_GAME_SLOTS + 0x04: 0,    # primary buffer: delete, Release
             DS_GAME_SLOTS + 0x0c: 0}                             # RestoreAll (lost buffer)


def snd_volume_slots():
    v = dict(SND_SLOTS)
    v[0x3C] = (2, {}, {"com": True, "returns": ds_hresult})        # IDirectSoundBuffer::SetVolume
    v[0x18] = (2, {1: 4}, {"com": True, "returns": ds_hresult})    # IDirectSoundBuffer::GetVolume
    return v


# Destroy: Close is probed (own spec).
SND_DESTROY_PROBES = {**SND_PROBES, 0x005ff1c0: (0, "thiscall", "probe")}

# The inlined iterators' "past the end" branch (0x5fdeb0, 0x5fdf66): the
# iterator was just made for this+0x40 and is never at its end there;
# Restore's catch block (0x5fdf90..0x5fdf9f, see the top). Destroy: the
# folder string's inlined erase, as CGraphicsData's (CLEANUP_GFX_DESTROY):
# _Grow(0) never succeeds (0x5fddbc), the split leaves an unshared buffer
# (0x5fdde0).
SND_ITERATOR_END = (0x5fdeb0,)
SND_RESTORE_DEAD = (0x5fdf66, 0x5fdf90, 0x5fdf99, 0x5fdf9f)
SND_DESTROY_DEAD = (0x5fddbc, 0x5fdde0)

SPECS += [
    Spec("CSoundData_Construct", 0x005fdb60, "thiscall", [], new_fails=0.05, new_fails_size=0x2c,
         ignore_bytes=(0x10,)),  # the folder's allocator byte: an uninitialized local of the original
    Spec("CSoundData_Destroy", 0x005fdc90, "thiscall", [], presets=snd_world, callees=SND_DESTROY_PROBES,
         virtuals=SND_SLOTS, eax_mask=0, exempt_blocks=SND_DESTROY_DEAD),  # callers clobber EAX
    Spec("CSoundData_DeletingDestroy", 0x005fdc70, "thiscall", [anything], presets=snd_world,
         callees=SND_DESTROY_PROBES, virtuals=SND_SLOTS),
    Spec("CSoundData_Close", 0x005ff1c0, "thiscall", [], eax_mask=0, presets=snd_world, callees=SND_PROBES,
         virtuals=SND_SLOTS),
    Spec("CSoundData_ReleaseBuffers", 0x005fde20, "thiscall", [], eax_mask=0, presets=snd_world,
         callees=SND_PROBES, virtuals=SND_SLOTS, exempt_blocks=SND_ITERATOR_END),
    Spec("CSoundData_Restore", 0x005fdef0, "thiscall", [], eax_mask=0, presets=snd_world, callees=SND_PROBES,
         virtuals=SND_SLOTS, cover_until=0x005fdfb8, exempt_blocks=SND_RESTORE_DEAD),
    Spec("CSoundData_SetVolume", 0x005ff9f0, "thiscall", [anything], eax_mask=0, presets=snd_world,
         callees=DS_THROWERS, virtuals=snd_volume_slots()),
    Spec("CSoundData_GetVolume", 0x005ffa30, "thiscall", [], presets=snd_world, callees=DS_THROWERS,
         virtuals=snd_volume_slots(),
         virtual_effects={0x18: ds_write_out(1, lambda h: (h >> 8) & 0xFFFFFFFF)}),
]


# -- DetachFromMedium and FindDirectoryContainingFile (as the GFX ones) -----

def snd_detach_world(rng, pool, args):
    fields = gfx_text_string(rng, 0x10, [b"C:\\S3\\Snd\\", b"D:\\Snd\\", b"A:", None])
    fields.append((0xC, rng.choice([0, 1, 1, 1, rng.getrandbits(8)]), 1))
    for i in range(2):
        fields += [(0x30 + 4 * i, rng.getrandbits(32) | 1 if rng.random() < 0.6 else 0, 4),
                   (0x20 + 4 * i, rng.getrandbits(32), 4)]
    return fields


# Dead: everything after the throwers (they never return): the message
# clean-ups of Detach (0x5ff3cf..0x5ff482) and FindDirectory (the "flag set"
# halves 0x5fe0e0, 0x5fe1fb, 0x5fe373, 0x5fe4f6, 0x5fe62e); Detach's release
# of the "" temporary's buffer (0x5ff363..0x5ff377: an empty string has
# none; DetachMemory never fails anyway); the null check of a stack buffer
# in FindDirectory (0x5fe3fc).
CLEANUP_SND_DETACH = (0x5ff363, 0x5ff36d, 0x5ff371, 0x5ff377, 0x5ff3cf, 0x5ff3e6, 0x5ff3ed, 0x5ff3f1,
                      0x5ff41b, 0x5ff41f, 0x5ff421, 0x5ff425, 0x5ff44a, 0x5ff455, 0x5ff459, 0x5ff465,
                      0x5ff46f, 0x5ff473, 0x5ff479, 0x5ff482)
CLEANUP_SND_FIND = (0x5fe0e0, 0x5fe1fb, 0x5fe373, 0x5fe3fc, 0x5fe4f6, 0x5fe62e)

SPECS += [
    Spec("CSoundData_DetachFromMedium", 0x005ff230, "thiscall", [anything], eax_mask=0,  # callers clobber EAX
         presets=snd_detach_world,
         callees={**GFX_THROWERS, 0x0061ab60: (0, "thiscall", detach_result, None, detach_probe_record)},
         exempt_blocks=CLEANUP_SND_DETACH),
    Spec("CSoundData_FindDirectoryContainingFile", 0x005fdfc0, "thiscall",
         [one_of(WORLD_BASE + GFX_RESULT), one_of(WORLD_BASE + GFX_FOLDERS), one_of(WORLD_BASE + GFX_FILE)],
         presets=gfx_folders, callees=GFX_THROWERS, ignore_bytes=(GFX_RESULT,), cover_until=0x005fe752,
         new_fails=0.05, new_fails_size=0x28, exempt_blocks=CLEANUP_SND_FIND),
]


# -- Open ----------------------------------------------------------------------
#
# Connect runs (LoadLibraryA / GetProcAddress API probes, DirectSoundCreate
# as the DS_PROC_SLOT virtual, which writes the fake IDirectSound);
# g_directSound points at the object itself (as its constructor sets it),
# so CPrimaryBuffer::Create reaches the fake IDirectSound through +4.
# FindDirectoryContainingFile and the file calls are probed as in the GFX
# Open spec; MapView returns fake SND headers.

def snd_map_view_effect(machine, ecx, args, h):
    return WORLD_BASE + SND_HEADERS + 0x100 * (h & 3)


def snd_open_world(rng, pool, args):
    fields = [(0, FAKE_VTABLE + DS_GAME_SLOTS, 4), (4, rng.getrandbits(32), 4)]
    fields += [(DS_IFACE + 0x10 * k, FAKE_VTABLE, 4) for k in range(4)]
    fields += gfx_text_string(rng, 0x10, [None, b"C:\\old\\"])
    fields += [(0x28, WORLD_BASE + SND_MAPPINGS, 4), (0x2C, WORLD_BASE + SND_MAPPINGS + 0x40, 4),
               (0x148, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 7]), 4)]
    fields += gfx_text_string(rng, GFX_BASE, [b"", b"C:\\S3\\", None, b"..\\"])
    for k in range(4):
        header = SND_HEADERS + 0x100 * k
        fields += [(header + 0x14, rng.choice([0xFFFFFFFF, 1, 2]), 4),
                   (header + 0x18, rng.choice([0x1C, rng.randrange(0x400)]), 4)]
    return fields


def snd_open_globals(rng, pool, args):
    return [(DS_GLOBAL, WORLD_BASE, 4)]


SND_OPEN_CALLEES = {
    **GFX_THROWERS, **DS_THROWERS,
    0x005fdfc0: (3, "thiscall", "probe", find_directory_effect, find_directory_record),  # FindDirectory
    0x00619610: (7, "thiscall", "probe", None, text_record),                             # CFile::Open
    0x0061a080: (1, "thiscall", "probe"),                                                # CFile::GetSize
    0x0061a520: (6, "thiscall", "probe"),                                                # CFileMapping::Create
    0x0061a7d0: (4, "thiscall", "probe", snd_map_view_effect),                           # CFileMapping::MapView
}

SND_OPEN_VIRTUALS = {
    DS_PROC_SLOT: (3, {1: 4}, {"com": True, "returns": ds_hresult}),  # DirectSoundCreate
    0x18: (3, {}, {"com": True, "returns": ds_hresult}),               # IDirectSound::SetCooperativeLevel
    0x0C: (4, {1: 0x14, 2: ()}, {"com": True, "returns": ds_hresult}),  # IDirectSound::CreateSoundBuffer
    0x38: (2, {1: 0x10}, {"com": True, "returns": ds_hresult}),        # IDirectSoundBuffer::SetFormat
    0x30: (4, {}, {"com": True, "returns": ds_hresult}),               # IDirectSoundBuffer::Play
    DS_GAME_SLOTS + 0x04: 0,                                           # g_directSound's slot 1 (lost buffer)
}

# The catch blocks and their rethrows (see the top of this file); dead: the
# strlen of the "" literal when its pointer is null (0x5feef6) and the
# fallback for a null path text (0x5ff0a5; the path is never empty). The
# CFile allocation failure (0x5fef47, 0x5fef51): any DirectSound failure
# ends an emulator run (a C++ exception), so the files are reached only
# after the primary buffer's allocation of the same size (0xc) succeeded,
# and new_fails fails every allocation of that size in a case.
SND_OPEN_CATCHES = (0x5fe8ec, 0x5fe8fd, 0x5fe90b, 0x5fe919, 0x5fe922, 0x5fe926, 0x5fe92e, 0x5fe945,
                    0x5fe97f, 0x5fe990, 0x5fe994, 0x5fe99c, 0x5fe9d5, 0x5fea08,
                    0x5feace, 0x5feae3, 0x5feae7, 0x5feaef, 0x5feb02, 0x5feea1,
                    0x5feef6, 0x5ff0a5, 0x5fef47, 0x5fef51)

SPECS += [
    Spec("CSoundData_Open", 0x005fe760, "thiscall",
         [anything, one_of(WORLD_BASE + GFX_BASE), one_of(0, 1, 2, 2, 3, 3, 7), one_of(1, 2, 4, 4, 1, 2, 0, 3)],
         eax_mask=0, presets=snd_open_world, globals=snd_open_globals, callees=SND_OPEN_CALLEES,
         virtuals=SND_OPEN_VIRTUALS,
         virtual_effects={DS_PROC_SLOT: ds_write_out(1, lambda h: WORLD_BASE + DS_IFACE, always=True),
                          0x0C: ds_write_out(2, ds_new_buffer, always=True)},
         new_fails=0.03, new_fails_size=(0x0c,), cover_until=0x005ff1bd, max_cases=600,
         exempt_blocks=SND_OPEN_CATCHES),
]
