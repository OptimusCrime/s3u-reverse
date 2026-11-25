"""Emulator specs, part `watcom`: specs of the replacements in src/s3re/watcom/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


# -- batch 24: Watcom pointer vector (watcom/ptr_vector.cpp) ----------------
#
# The vector at world offset 0 (ECX = the world): vtable = emu.FAKE_VTABLE
# (slot 0x04 is the "length changed" call), data in scratch 0.

VECTOR_VIRTUALS = {0x04: 1}


def vector_state(rng, pool, args):
    capacity = rng.choice([0, 0, 1, 2, rng.randrange(8), rng.getrandbits(32)])
    length = rng.choice([0, 0, 1, 2, 3, rng.randrange(8), 0x80000000, 0x80000001, rng.getrandbits(32)])
    return [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH, 4), (0xc, capacity, 4),
            (0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4), (0x14, length, 4)]

SPECS += [
    # n * 4 wraps for large n; the fake heap refuses blocks it can't hold.
    Spec("PtrVector_Resize", 0x00429a00, "thiscall", [small_or_anything(0, 6)], presets=vector_state),
    Spec("PtrVector_ResizeClampLength", 0x00437fe0, "thiscall", [small_or_anything(0, 6)], presets=vector_state),
    # 0x428f7f (clamp the length to 1 after growing) is dead: this path is
    # only taken when the length was 0.
    Spec("PtrVector_Index", 0x00428ee0, "thiscall", [small_or_anything(-3, 8)], presets=vector_state,
         virtuals=VECTOR_VIRTUALS, new_fails=0.2, exempt_blocks=(0x428f7f,)),
]


# -- batch 27: Watcom vector of strings, resize (watcom/string_vector.cpp) ---
#
# The vector at world offset 0; its entries (16-byte strings) in scratch
# slot 0, their buffers in slots 1.. (one 0x100-byte area per entry, some
# shared). The new strings' allocator byte is the low byte of n.


def string_vector_state(rng, pool, args):
    capacity = rng.choice([0, 1, 2, 3, 4, 5, 6])
    filled = rng.randint(0, capacity) if rng.random() < 0.9 else rng.randint(-2, 0) & 0xFFFFFFFF
    fields = [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH if capacity else rng.choice([0, WORLD_BASE + SCRATCH]), 4),
              (0xc, capacity, 4), (0x10, filled, 4), (0x14, rng.randrange(8), 4)]
    ptrs = []
    for i in range(capacity):
        shared = rng.choice(ptrs) if ptrs and rng.random() < 0.3 else None
        entry, ptr = string_fields(rng, SCRATCH + 16 * i, SCRATCH + SCRATCH_SLOT_BYTES + 0x100 * i,
                                   shared_ptr=shared)
        fields += entry
        if ptr:
            ptrs.append(ptr)
    return fields

SPECS += [
    # Dead, in the inlined destructor of the old entries: 0x40a6d0 is the
    # _Eos after erase(0, npos) (new length 0: _Grow returns false); 0x40a6fc
    # decrements the count byte, but the split before the erase left the
    # buffer unshared (count 0) or frozen.
    Spec("StringVector_Resize", 0x0040a580, "thiscall", [small_or_anything(0, 8)], presets=string_vector_state,
         new_fails=0.1, exempt_blocks=(0x0040a6d0, 0x0040a6fc)),
]


# -- WCBareVectorBase<unsigned short> constructor (ushort_vector.cpp, session 55) --
# n small, 0, or any (n * 2 wraps); operator new fails in some cases.
SPECS += [
    Spec("WCBareVectorBase_UShort_Construct", 0x00462df0, "thiscall",
         [lambda rng, pool: rng.choice([0, 0, 1, 10, rng.randrange(64), rng.getrandbits(32), 0x80000000])],
         new_fails=0.15),
]
