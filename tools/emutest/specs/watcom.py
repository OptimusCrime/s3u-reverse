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
SPECS += [Spec("VectorDestructor_414240", 0x00414240, "thiscall", [], presets=watcom_vector_to_destroy,
               callees={0x004a04d0: (0, "thiscall", "probe")})]


# -- list bases, skip-list iterators, vector callbacks (session 56) ---------
SPECS += [Spec(f"IsvListConstructor_{a:x}", a, "thiscall", []) for a in (0x41b080, 0x428bd0, 0x428bf0)]
SPECS += [Spec(f"SkipListIterConstructor_{a:x}", a, "thiscall", [anything]) for a in (0x4726e0, 0x472780, 0x499600)]
SPECS += [Spec(name, address, "stdcall", [anything, anything], callees={member: (1, "thiscall", "probe")})
          for name, address, member in (("PlayerVectorEquivalent", 0x437530, 0x49f100),
                                        ("TeamVectorLessThan", 0x437550, 0x4a05a0),
                                        ("LobbyVectorEquivalent", 0x44c4a0, 0x606ac0))]


# -- inline comparison callbacks (vector_callbacks.cpp, session 56) ---------
# a and b point into scratch slots 0 and 1 (offset 0x20, so the skip-list
# keys before them are inside); slot 1 often repeats slot 0's bytes. The
# string callback gets strings there (buffers in slots 2 and 3).


def callback_buffers(rng, pool, args):
    a = [rng.getrandbits(8) for _ in range(0x160)]
    b = list(a) if rng.random() < 0.7 else [rng.getrandbits(8) for _ in range(0x160)]
    for _ in range(rng.randrange(3)):
        b[rng.randrange(0x160)] = rng.getrandbits(8)
    if rng.random() < 0.3:
        b[0x20 - 0x14 + rng.randrange(0x18)] ^= 1
    return ([(SCRATCH + i, v, 1) for i, v in enumerate(a)] +
            [(SCRATCH + SCRATCH_SLOT_BYTES + i, v, 1) for i, v in enumerate(b)])


def string_callback_buffers(rng, pool, args):
    fields = callback_buffers(rng, pool, args)
    letters = [0x61, 0x62, 0x80, 0xff]
    first = None
    for slot in (0, 1):
        length = rng.randrange(13)
        text = [first[k] if first and k < len(first) and rng.random() < 0.75 else rng.choice(letters)
                for k in range(length)]
        first = first or text
        buffer = SCRATCH + (2 + slot) * SCRATCH_SLOT_BYTES
        none = length == 0 and rng.random() < 0.5
        base = SCRATCH + slot * SCRATCH_SLOT_BYTES + 0x20
        fields += [(buffer + k, v, 1) for k, v in enumerate(text + [0])]
        fields += [(base + 4, 0 if none else WORLD_BASE + buffer, 4), (base + 8, length, 4)]
    return fields


CALLBACK_ARGS = [lambda rng, pool: WORLD_BASE + SCRATCH + 0x20,
                 lambda rng, pool: WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 0x20]
SPECS += [Spec(name, address, "stdcall", CALLBACK_ARGS, presets=callback_buffers)
          for name, address in (("UShortVectorEquivalent", 0x462850), ("ChunkDescriptionEquivalent", 0x499050),
                                ("SkipDictKeyLess_457a70", 0x457a70), ("SkipDictKeyLess_4a6b20", 0x4a6b20),
                                ("SkipDictKeyLess_428100", 0x428100), ("SkipDictKeyEqual_4a6b00", 0x4a6b00))]
SPECS += [Spec("StringVectorLessThan", 0x0044c910, "stdcall", CALLBACK_ARGS, presets=string_callback_buffers)]


# -- base_init_upto (vector_init.cpp, session 56) ---------------------------
# The vector at world offset 0, its block 2 elements into scratch slot 0
# (filled may be -2); the element constructors probed (the CTeam one is
# ours, with its own spec). The library declares the function void; EAX is
# left over (callers don't read it).


def init_upto_state(size):
    def presets(rng, pool, args):
        n = args[0] - (1 << 32) if args[0] & 0x80000000 else args[0]
        # A null block only with filled 0 and n <= 0 (other elements would
        # be wild addresses in both versions).
        data = rng.choice([WORLD_BASE + SCRATCH + 2 * size] * 3 + ([0] if n <= 0 else []))
        filled = rng.randint(-2, 8) if data else 0
        return [(8, data, 4), (0x10, filled & 0xFFFFFFFF, 4)]
    return presets


def init_upto_n(rng, pool):
    return rng.randint(-2, 8) & 0xFFFFFFFF


INIT_UPTO_CONSTRUCTORS = {0x4274a0: (0, "thiscall", "probe"), 0x4276b0: (0, "thiscall", "probe"),
                          0x49e8f0: (0, "thiscall", "probe"), 0x4a0200: (0, "thiscall", "probe"),
                          0x5bb040: (0, "thiscall", "probe")}
SPECS += [Spec(f"InitUpTo_{address:x}", address, "thiscall", [init_upto_n],
               presets=init_upto_state(size), eax_mask=0, callees=INIT_UPTO_CONSTRUCTORS)
          for address, size in ((0x428e50, 4), (0x40a1d0, 16), (0x429040, 0x20), (0x4293d0, 0x14),
                                (0x414650, 0xcc), (0x414b10, 0x40), (0x463350, 0x60))]


# -- WCSkipListIterBase::operator++ (list_iterators.cpp, session 56) --------
# The iterator at world offset 0; the dictionary in scratch slot 0 (header
# pointer at +0x70 -> a header node at +0xb0), a chain of up to 4 nodes at
# +0x80 + 0x10 k.


def skip_iter_state(rng, pool, args):
    base = WORLD_BASE + SCRATCH
    nodes = rng.randrange(5)
    fields = [(SCRATCH + 0x70, base + 0xb0, 4)]
    for k in range(nodes + 1):
        at = 0xb0 if k == 0 else 0x80 + 0x10 * (k - 1)
        fields.append((SCRATCH + at, base + 0x80 + 0x10 * k if k < nodes else 0, 4))
    current = 0 if nodes == 0 or rng.random() < 0.4 else base + 0x80 + 0x10 * rng.randrange(nodes)
    fields += [(8, rng.choice([0, base, base, base]), 4), (0xc, current, 4),
               (0x10, rng.choice([0, 0, 0, 1, 2]), 4)]
    return fields


SPECS += [Spec("SkipListIter_Increment", 0x00499df0, "thiscall", [], presets=skip_iter_state)]


# -- resize / insertAt of plain-element vectors (vector_insert.cpp, session 56) --
# The vector at world offset 0 with its block in scratch slot 0 (or none),
# capacity 0..6, filled -1..capacity, length 0..capacity, grow 0..3 or odd
# (2^30: the block size wraps; -1: the vector shrinks by one, which reaches
# the keep = new capacity, no-block and length clamp branches); the inserted value in scratch slot 3. Operator new fails in
# some cases.


def plain_vector_state(rng, pool, args):
    capacity = rng.randrange(7)
    data = WORLD_BASE + SCRATCH if capacity or rng.random() < 0.5 else 0
    return [(8, data, 4), (0xc, capacity, 4), (0x10, rng.randint(-1, capacity) & 0xFFFFFFFF, 4),
            (0x14, rng.randint(0, capacity), 4), (0x18, rng.choice([0, 1, 2, 3, 0x40000000, 0xFFFFFFFF]), 4)]


INSERT_INDEX = lambda rng, pool: rng.choice([rng.randint(-2, 8), rng.getrandbits(32)]) & 0xFFFFFFFF  # noqa: E731
SPECS += [
    Spec("ChunkDescriptionBareResize", 0x00499790, "thiscall", [small_or_anything(0, 8)],
         presets=plain_vector_state, new_fails=0.15),
    Spec("ChunkDescriptionResize", 0x00499fb0, "thiscall", [small_or_anything(0, 8)],
         presets=plain_vector_state, new_fails=0.15),
    Spec("WordVectorInsertAt", 0x00428d10, "thiscall", [INSERT_INDEX, scratch(3)],
         presets=plain_vector_state, new_fails=0.15),
    Spec("ChunkDescriptionInsertAt", 0x00499980, "thiscall", [INSERT_INDEX, scratch(3)],
         presets=plain_vector_state, new_fails=0.15),
]
