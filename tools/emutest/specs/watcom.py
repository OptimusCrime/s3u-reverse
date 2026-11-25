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


# -- Watcom vector constructors and destructors (bare_vectors.cpp, session 56) --
#
# The vector at world offset 0. Destructors: data 0 or a block in scratch
# slot 0, filled -2..6 (the player-vector destructors call
# ~GamePlayerSetup 0x49ec80 per element: probed). Constructors: capacity
# small, 0 or any (the product wraps), operator new fails in some cases.

WATCOM_TRIVIAL_VECTOR_DESTRUCTORS = (
    0x4143b0, 0x427de0, 0x427e40, 0x437570, 0x44caa0, 0x44cca0, 0x457a10, 0x462870, 0x462b00,
    0x4721c0, 0x472240, 0x4722e0, 0x472360, 0x472440, 0x4990c0, 0x4992f0, 0x44e240, 0x462eb0,
    0x462f10, 0x466190, 0x472660, 0x472bb0, 0x472c10, 0x472e00, 0x473000, 0x473060, 0x4a1140)
WATCOM_PLAYER_VECTOR_DESTRUCTORS = (0x4141a0, 0x4a11a0)
WATCOM_VECTOR_CONSTRUCTORS = (
    0x437630, 0x44ce10, 0x462c70, 0x466110, 0x472700, 0x4727a0, 0x4728a0, 0x472920, 0x44ce90,
    0x457fb0, 0x457f20, 0x458500, 0x457ea0, 0x472820, 0x462cf0)
WATCOM_BARE_VECTOR_CONSTRUCTORS = (0x437c90, 0x458a60, 0x4586f0, 0x4377a0)


def watcom_vector_to_destroy(rng, pool, args):
    return [(8, rng.choice([0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH]), 4),
            (0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4)]


def watcom_capacity(rng, pool):
    return rng.choice([0, 0, 1, 2, rng.randrange(65), rng.randrange(65), rng.getrandbits(32),
                       0x40000000 + rng.randrange(4), 0x80000000])


SPECS += [Spec(f"VectorDestructor_{a:x}", a, "thiscall", [], presets=watcom_vector_to_destroy)
          for a in WATCOM_TRIVIAL_VECTOR_DESTRUCTORS]
SPECS += [Spec(f"VectorDestructor_{a:x}", a, "thiscall", [], presets=watcom_vector_to_destroy,
               callees={0x0049ec80: (0, "thiscall", "probe")})
          for a in WATCOM_PLAYER_VECTOR_DESTRUCTORS]
SPECS += [Spec(f"VectorConstructor_{a:x}", a, "thiscall", [watcom_capacity, anything], new_fails=0.2)
          for a in WATCOM_VECTOR_CONSTRUCTORS]
SPECS += [Spec(f"BareVectorConstructor_{a:x}", a, "thiscall", [watcom_capacity], new_fails=0.2)
          for a in WATCOM_BARE_VECTOR_CONSTRUCTORS]

# The string-vector destructors (string_vector.cpp, session 56) on the same
# vectors as StringVector_Resize. Dead in the inlined ~string: the _Eos
# after erase(0, npos) (new length 0: _Grow returns false), as 0x40a6d0 in
# StringVector_Resize.
SPECS += [Spec(f"StringVectorDestructor_{a:x}", a, "thiscall", [], presets=string_vector_state,
               exempt_blocks=(dead,))
          for a, dead in ((0x409d50, 0x409df3), (0x44c980, 0x44ca23), (0x40a280, 0x40a323),
                          (0x44cf50, 0x44cff3))]
