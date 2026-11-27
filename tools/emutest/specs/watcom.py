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
WATCOM_BARE_VECTOR_CONSTRUCTORS = (0x437c90, 0x458a60, 0x4586f0, 0x4377a0, 0x428c10, 0x428e70, 0x429080, 0x429410)


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
SPECS += [Spec(f"SkipListIterConstructor_{a:x}", a, "thiscall", [anything]) for a in (0x4726e0, 0x472780, 0x499600,
                                                                                       0x5c34f0, 0x5ffc60)]
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
                                ("SkipDictKeyLess_428100", 0x428100), ("SkipDictKeyEqual_4a6b00", 0x4a6b00),
                                ("SkipDictKeyEqual_5c33a0", 0x5c33a0), ("SkipDictKeyLess_5c33c0", 0x5c33c0))]
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


# -- string vector insertAt / removeAt (string_vector.cpp, session 56) ------
# A consistent vector at world offset 0: count strings (entries in scratch
# slot 0, buffers in slots 1..), filled = length = count, capacity count or
# up to 2 more, grow 0..2 or -1 (the vector shrinks by one: the length
# clamp after the resize); the value string at scratch slot 7 (its buffer in
# slot 8; sometimes sharing an element's buffer), or an element itself.
STRING_VALUE = SCRATCH + 7 * SCRATCH_SLOT_BYTES


def string_vector_room(rng, pool, args):
    count = rng.randrange(6)
    if len(args) > 1 and args[1] != WORLD_BASE + STRING_VALUE:
        count = rng.randint(3, 5)  # the value is one of the first three elements
    capacity = count + rng.randrange(3)
    fields = [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH if capacity else rng.choice([0, WORLD_BASE + SCRATCH]), 4),
              (0xc, capacity, 4), (0x10, count, 4), (0x14, count, 4), (0x18, rng.choice([0, 1, 2, 0xFFFFFFFF]), 4)]
    ptrs = []
    for i in range(count):
        shared = rng.choice(ptrs) if ptrs and rng.random() < 0.3 else None
        entry, ptr = string_fields(rng, SCRATCH + 16 * i, SCRATCH + SCRATCH_SLOT_BYTES + 0x100 * i,
                                   shared_ptr=shared)
        fields += entry
        if ptr:
            ptrs.append(ptr)
    value, _ = string_fields(rng, STRING_VALUE, STRING_VALUE + SCRATCH_SLOT_BYTES,
                             shared_ptr=rng.choice(ptrs) if ptrs else None)
    return fields + value


def string_value(rng, pool):
    if rng.random() < 0.15:
        return WORLD_BASE + SCRATCH + 16 * rng.randrange(3)  # an element (if there is one)
    return WORLD_BASE + STRING_VALUE


# globals: name_null_string_count (ui.py's:
# the static empty string's count byte 0, 1, 0xfe or 0xff, so a source
# without buffer sometimes takes the copy path).
# Dead, in the inlined operator= (assign(x, 0, npos)): the count clamp to
# npos (lengths are at most npos) and the self-assignment path (insertAt:
# its _Xran, as the count is the string's own length; removeAt: never an
# element from itself).
SPECS += [
    Spec("StringVector_InsertAt", 0x004148a0, "thiscall", [lambda rng, pool: rng.randint(-2, 7) & 0xFFFFFFFF, string_value],
         presets=string_vector_room, globals=name_null_string_count, new_fails=0.1,
         exempt_blocks=(0x004149f8, 0x00414a0b)),
    Spec("StringVector_RemoveAt", 0x00429750, "thiscall", [lambda rng, pool: rng.randrange(5)],
         presets=string_vector_room, globals=name_null_string_count,
         exempt_blocks=(0x00429793, 0x00429799, 0x004297a2, 0x004297a7, 0x004297b7, 0x004297b9, 0x004297bd,
                        0x004297e5, 0x004297ed)),
]


# The string-vector copy constructor (session 56): the source is the vector
# StringVector_Resize gets (world offset 0); the new vector in scratch slot 9.
SPECS += [Spec("StringVector_CopyConstruct", 0x00409c40, "fastcall",
               [one_of(WORLD_BASE + SCRATCH + 9 * SCRATCH_SLOT_BYTES), anything, one_of(WORLD_BASE)],
               presets=string_vector_state, new_fails=0.1)]


# -- the sound code's skip-list destructors (skiplist_dicts.cpp, session 57) --
# A random dictionary at world 0 (count sometimes 0); clear and the pair's
# ~CDirectSoundBuffer probed (the library and verified code, own specs);
# the empty error hook 0x59f200 runs.
SKIP_DESTROY_CALLEES = {0x0060bd80: (0, "thiscall", "probe"), 0x005c1fe0: (0, "thiscall", "probe")}


def skip_dict_count(rng, pool, args):
    return [(0x74, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4)]


def skip_iter_node(rng, pool, args):
    return [(0x0C, rng.choice([0, rng.getrandbits(32)]), 4)]


SPECS += [Spec(f"SkipListDestructor_{a:x}", a, "thiscall", [], presets=skip_dict_count, callees=SKIP_DESTROY_CALLEES)
          for a in (0x5c3450, 0x5ffa60, 0x5ffb60, 0x5ffac0, 0x5ffc00, 0x5c33e0)]
# cover_until: Ghidra's function also takes in the static initializer that
# follows it (0x5ffc90).
SPECS += [Spec("SoundEffectDictIter_KeyValue", 0x005ffc80, "thiscall", [], presets=skip_iter_node,
               cover_until=0x005ffc8e)]


# -- the intrusive singly linked list (isv_slist.cpp, wip/ddraw-render) ------
# The list at world 0 (+8 the last link, +0xc the count, +0x10 the
# allocator), a circle of up to 12 links at ISV_LINKS in random order; an
# iterator at ISV_ITER (+8 the list, +0xc the current link, +0x10 state).
# The list's vtable is FAKE_VTABLE (slot 1: destroy a link); the allocator
# is a probe (fastcall size in ECX: recorded with ECX) that hands out
# ISV_BLOCK or null.
ISV_LINKS = 0x100
ISV_ITER = 0x80
ISV_VALUE = 0x60
ISV_BLOCK = SCRATCH + 70 * SCRATCH_SLOT_BYTES
ISV_ALLOC_SLOT = 0x3B0
ISV_VIRTUAL_CODE = FAKE_VTABLE + 0x1000   # emu.VIRTUAL_CODE


def isv_link(k):
    return WORLD_BASE + ISV_LINKS + 8 * k


def isv_world(rng, pool, args):
    links = 0 if rng.random() < 0.15 else rng.randint(1, 12)
    order = list(range(links))
    rng.shuffle(order)
    fields = [(0, FAKE_VTABLE, 4)]
    for i, k in enumerate(order):
        fields.append((ISV_LINKS + 8 * k, isv_link(order[(i + 1) % links]), 4))
    count = links if links == 0 or rng.random() < 0.75 else rng.choice([0, 1, 5, 20])
    fields += [(8, isv_link(order[-1]) if links else 0, 4), (0xC, count, 4),
               (0x10, rng.choice([0, ISV_VIRTUAL_CODE + ISV_ALLOC_SLOT]), 4),
               (ISV_ITER + 8, rng.choice([0, WORLD_BASE, WORLD_BASE, WORLD_BASE]), 4),
               (ISV_ITER + 0xC, rng.choice([0, isv_link(rng.choice(order))]) if links else 0, 4),
               (ISV_ITER + 0x10, rng.choice([0, 0, 2, rng.getrandbits(32)]), 4)]
    return fields


def isv_link_arg(rng, pool):
    return isv_link(rng.randrange(12)) if rng.random() < 0.9 else 0


def isv_index(rng, pool):
    return rng.choice([rng.randrange(14), -rng.randrange(3) & 0xFFFFFFFF, rng.randrange(40), rng.getrandbits(32)])


def isv_allocate(h):
    return 0 if h % 4 == 0 else WORLD_BASE + ISV_BLOCK


ISV_VIRTUALS = {0x04: 1, ISV_ALLOC_SLOT: (0, {}, {"returns": isv_allocate})}


def isv_spec(name, address, args, **kwargs):
    return Spec(name, address, "thiscall", args, presets=isv_world, virtuals=ISV_VIRTUALS, **kwargs)


SPECS += [
    # base_next(link, reset): the link is one of the list's when it is not reset.
    isv_spec("WCIsvSListBase_Next", 0x0060b400, [isv_link_arg, one_of(0, 0, 1, 0x100)]),
    isv_spec("WCIsvSListBase_IndexCheck", 0x0060b460, [isv_index]),
    isv_spec("WCIsvSListBase_BeforeFind", 0x0060b4b0, [isv_index]),
    isv_spec("WCIsvSListBase_Get", 0x0060b500, [isv_index]),
    isv_spec("WCIsvSListBase_Clear", 0x0060b550, []),
    isv_spec("WCIsvSListBase_ClearAndDestroy", 0x0060b560, []),
    Spec("WCIsvSListIter_Advance", 0x0060b5f0, "fastcall",
         [one_of(WORLD_BASE + ISV_ITER), anything, isv_index], presets=isv_world, virtuals=ISV_VIRTUALS),
    isv_spec("WCPtrListBase_NewLink", 0x00463b50, [one_of(WORLD_BASE + ISV_VALUE)], new_fails=0.2),
]


# -- wip/watcom-r6: more instantiations of the vector constructors and
# destructors (bare_vectors.cpp), on the presets above.
WATCOM_R6_TRIVIAL_VECTOR_DESTRUCTORS = (0x4145f0, 0x42f8c0, 0x499630, 0x49b7f0, 0x541690, 0x5b7c10, 0x5de270,
                                        0x5de2d0, 0x5f3de0, 0x61aab0)
WATCOM_R6_VECTOR_CONSTRUCTORS = (0x414410, 0x427d60, 0x468330, 0x499580, 0x609f50, 0x414510, 0x4993f0, 0x5fda00,
                                 0x5dde00, 0x5de010, 0x5de090, 0x5f3f40)
WATCOM_R6_BARE_VECTOR_CONSTRUCTORS = (0x5de130, 0x4683b0)
SPECS += [Spec(f"VectorDestructor_{a:x}", a, "thiscall", [], presets=watcom_vector_to_destroy)
          for a in WATCOM_R6_TRIVIAL_VECTOR_DESTRUCTORS]
SPECS += [Spec("VectorDestructor_4146d0", 0x004146d0, "thiscall", [], presets=watcom_vector_to_destroy,
               callees={0x0049ec80: (0, "thiscall", "probe")})]
SPECS += [Spec(f"VectorConstructor_{a:x}", a, "thiscall", [watcom_capacity, anything], new_fails=0.2)
          for a in WATCOM_R6_VECTOR_CONSTRUCTORS]
SPECS += [Spec(f"BareVectorConstructor_{a:x}", a, "thiscall", [watcom_capacity], new_fails=0.2)
          for a in WATCOM_R6_BARE_VECTOR_CONSTRUCTORS]
# libs-r7: three more instantiations of the same code.
SPECS += [Spec("VectorDestructor_5bdf70", 0x005bdf70, "thiscall", [], presets=watcom_vector_to_destroy),
          Spec("VectorConstructor_5b7ec0", 0x005b7ec0, "thiscall", [watcom_capacity, anything], new_fails=0.2),
          Spec("VectorConstructor_5b7f40", 0x005b7f40, "thiscall", [watcom_capacity, anything], new_fails=0.2),
          Spec("BareVectorConstructor_499690", 0x00499690, "thiscall", [watcom_capacity], new_fails=0.2)]


# -- destructors of vectors whose elements have a destructor (element_vectors.cpp) --
# The vector at world offset 0, its block (or none) in scratch slot 0 with
# room for 6 elements; filled -2..6. Virtual elements: each element's
# vtable is FAKE_VTABLE (slot 0, the scalar deleting destructor, probed with
# its flags). ~CColumn / ~CLine probed.
def element_vector_to_destroy(size):
    def presets(rng, pool, args):
        data = rng.choice([0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH])
        fields = [(8, data, 4), (0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4)]
        return fields + [(SCRATCH + k * size, FAKE_VTABLE, 4) for k in range(6)]
    return presets


VIRTUAL_ELEMENT_VECTOR_DESTRUCTORS = (
    (0x44c4e0, 0x54), (0x44d290, 0x54), (0x5de3b0, 0x54), (0x5de450, 0x54),
    (0x44c580, 0x11c), (0x44d690, 0x11c), (0x458770, 0x11c), (0x458980, 0x11c),
    (0x44cbe0, 0x20), (0x44dd10, 0x20), (0x44de40, 0x20), (0x458d80, 0x20),
    (0x609940, 0x14), (0x60a130, 0x14), (0x60a590, 0x14), (0x60a630, 0x14),
    (0x44cb20, 0x17c), (0x44db90, 0x17c), (0x5dfd90, 0x17c),
    (0x4627b0, 0x40), (0x468420, 0x40), (0x472b10, 0x40))
SPECS += [Spec(f"ElementVectorDestructor_{a:x}", a, "thiscall", [], presets=element_vector_to_destroy(size),
               virtuals={0x00: 1})
          for a, size in VIRTUAL_ELEMENT_VECTOR_DESTRUCTORS]
SPECS += [Spec(f"ElementVectorDestructor_{a:x}", a, "thiscall", [], presets=element_vector_to_destroy(size),
               callees={destroy: (0, "thiscall", "probe")})
          for a, size, destroy in ((0x427fe0, 0x20, 0x4275e0), (0x4290f0, 0x20, 0x4275e0),
                                   (0x428070, 0x14, 0x4277d0), (0x429480, 0x14, 0x4277d0))]


# -- the other skip-list destructors (skiplist_dicts.cpp, wip/watcom-r6) ----
SPECS += [Spec(f"SkipListDestructor_{a:x}", a, "thiscall", [], presets=skip_dict_count, callees=SKIP_DESTROY_CALLEES)
          for a in (0x428120, 0x4283a0, 0x457bb0, 0x457cb0, 0x45d500, 0x472120, 0x472580, 0x472600, 0x499290,
                    0x4a6a40, 0x4a6b40, 0x4a6c40, 0x4a6ce0, 0x4a8e10, 0x4a8eb0, 0x609bb0, 0x616050)]
SPECS += [Spec("SkipListDestructor_616150", 0x00616150, "thiscall", [], presets=skip_dict_count,
               callees=SKIP_DESTROY_CALLEES)]  # (libs-r7)
SPECS += [Spec("SkipListDestructor_609aa0", 0x00609aa0, "thiscall", [], presets=skip_dict_count,
               callees={0x0060bd80: (0, "thiscall", "probe"), 0x006014b0: (0, "thiscall", "probe")})]


# -- list destructors (slist_destructors.cpp, wip/watcom-r6) ----------------
# clearAndDestroy and ~WCIsvListBase (ours, own specs) probed; the list's
# last link 0 or not.
def slist_last(rng, pool, args):
    return [(8, rng.choice([0, 0, rng.getrandbits(32)]), 4)]


SLIST_CALLEES = {0x0060b560: (0, "thiscall", "probe"), 0x0060b5a0: (0, "thiscall", "probe")}
SPECS += [Spec(f"SListDestructor_{a:x}", a, "thiscall", [], presets=slist_last, callees=SLIST_CALLEES)
          for a in (0x427ea0, 0x427f10, 0x428290, 0x4628f0, 0x462bc0, 0x462f80, 0x5b7950, 0x5b79c0, 0x5b7a30,
                    0x5b7d90, 0x5b7e50, 0x5ddaf0, 0x5ddd70, 0x5dfe80)]
# cover_until: the 11 bytes before the int 3 padding (a tail jump, so
# no ret marks the end).
SPECS += [Spec(f"IsvSListDestructor_{a:x}", a, "thiscall", [], callees=SLIST_CALLEES, cover_until=a + 11)
          for a in (0x428fd0, 0x428fe0, 0x428ff0, 0x462f70, 0x5b7fd0, 0x5b7fe0, 0x5b8030, 0x5dfe30)]


# -- more list and iterator constructors, the base destructors (list_iterators.cpp) --
# The base destructors leave EAX as the caller had it (void).
SPECS += [
    Spec("IsvListConstructor_5de110", 0x005de110, "thiscall", []),
    Spec("SkipListIterConstructor_609fd0", 0x00609fd0, "thiscall", [anything]),
    Spec("ListIterConstructor_463be0", 0x00463be0, "thiscall", [anything]),
    Spec("ListIterConstructor_5b7d10", 0x005b7d10, "thiscall", [anything]),
    Spec("WCIterExcept_Destroy", 0x00499620, "thiscall", [], eax_mask=0),
    Spec("WCSkipNonTempBase_Destroy", 0x00423a10, "thiscall", [], eax_mask=0),
]


# -- member callbacks of the value and pointer vectors (vector_callbacks.cpp) --
# a and b point into scratch (random words); the member probed.
MEMBER_CALLBACKS = (
    (0x44cb00, 0x5dc780), (0x44cbc0, 0x5dc460), (0x462960, 0x5bb340), (0x462980, 0x5bb3a0), (0x4682f0, 0x5ba970),
    (0x541650, 0x5ec200), (0x541670, 0x5ec250), (0x5b7aa0, 0x5b69c0), (0x5b7bf0, 0x5b6c60), (0x5bdbb0, 0x5bd260),
    (0x5dd740, 0x5ebfd0), (0x5dd880, 0x5ed220), (0x5ddb80, 0x5c4830), (0x609920, 0x6027d0),
    (0x472180, 0x49d990), (0x4721a0, 0x49d9c0), (0x472220, 0x49d7f0), (0x4722a0, 0x49d940), (0x4722c0, 0x49d900),
    (0x472340, 0x49d9f0), (0x5b7930, 0x60ec00),
    (0x462ac0, 0x5c0f80), (0x462ae0, 0x5c0fa0), (0x5f3da0, 0x5ed220), (0x5f3dc0, 0x5ed3e0))
SPECS += [Spec(f"VectorCallback_{a:x}", a, "stdcall", CALLBACK_ARGS, callees={member: (1, "thiscall", "probe")})
          for a, member in MEMBER_CALLBACKS]


# The string-vector destructors of wip/watcom-r6, on the same vectors:
# 0x5df310 is 0x409d50's bytes; 0x414770 has the buffer split inlined too.
SPECS += [Spec("StringVectorDestructor_5df310", 0x005df310, "thiscall", [], presets=string_vector_state,
               exempt_blocks=(0x005df3b3,)),
          # Dead: 0x414840, the _Eos after erase(0, npos) (as 0x409df3).
          Spec("StringVectorDestructor_414770", 0x00414770, "thiscall", [], presets=string_vector_state,
               exempt_blocks=(0x00414840,))]


# -- vectors of elements with string members (element_vectors.cpp, wip/watcom-r6) --
# The vector at world 0, its block in scratch slot 0 (6 elements) or none,
# filled -2..6; every element's strings built by string_fields (buffers in
# slots 1.., 0x40 bytes each, some shared).
def string_element_vector(size, offsets):
    def presets(rng, pool, args):
        data = rng.choice([0, WORLD_BASE + SCRATCH, WORLD_BASE + SCRATCH])
        fields = [(8, data, 4), (0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4)]
        ptrs = []
        slot = 0
        for k in range(6):
            for offset in offsets:
                shared = rng.choice(ptrs) if ptrs and rng.random() < 0.3 else None
                entry, ptr = string_fields(rng, SCRATCH + k * size + offset,
                                           SCRATCH + SCRATCH_SLOT_BYTES + 0x40 * slot, shared_ptr=shared)
                slot += 1
                fields += entry
                if ptr:
                    ptrs.append(ptr)
        return fields
    return presets


# Dead in each inlined ~string: the _Eos after erase(0, npos) (new length
# 0: _Grow returns false), as in the string-vector destructors.
STRING_ELEMENT_VECTORS = (
    (0x4629a0, 0x60, (0x50,)), (0x463730, 0x60, (0x50,)), (0x5bde60, 0x60, (0x50,)), (0x4633d0, 0x60, (0x50,)),
    (0x5dd760, 0x18, (0x8,)), (0x5de5e0, 0x18, (0x8,)), (0x5dd8a0, 0x24, (0x14,)), (0x5deb60, 0x24, (0x14,)),
    (0x5dd9c0, 0x20, (0x10, 0)), (0x5df7a0, 0x20, (0x10, 0)), (0x5ddba0, 0x20, (0x10, 0)), (0x5e0390, 0x20, (0x10, 0)),
    (0x5dff70, 0x20, (0x10, 0)), (0x5b7ac0, 0x30, (0x20, 0x10)))
STRING_ELEMENT_DEAD = {0x4629a0: (0x462a46,), 0x463730: (0x4637d6,), 0x5bde60: (0x5bdf06,), 0x4633d0: (0x4634a3,),
                       0x5dd760: (0x5dd806,), 0x5de5e0: (0x5de6b3,), 0x5dd8a0: (0x5dd946,), 0x5deb60: (0x5dec33,),
                       0x5dd9c0: (0x5dda8c,), 0x5df7a0: (0x5df867,), 0x5ddba0: (0x5ddc6c,), 0x5e0390: (0x5e0457,),
                       0x5dff70: (0x5e001a,), 0x5b7ac0: (0x5b7b93,)}
SPECS += [Spec(f"ElementVectorDestructor_{a:x}", a, "thiscall", [], presets=string_element_vector(size, offsets),
               exempt_blocks=STRING_ELEMENT_DEAD.get(a, ()))
          for a, size, offsets in STRING_ELEMENT_VECTORS]
# libs-r7: three more, with ~string's erase (or _Split) called out of line.
# Dead: 0x5dea76 / 0x5df246 the _Eos after erase(0, npos) (as above);
# 0x5e01fe the count decrement of the inlined _Tidy(true) after erase(0,
# npos), which has made the string unshared (count 0) or frozen.
SPECS += [Spec(f"ElementVectorDestructor_{a:x}", a, "thiscall", [], presets=string_element_vector(size, offsets),
               exempt_blocks=dead)
          for a, size, offsets, dead in ((0x5de9d0, 0x18, (0x8,), (0x5dea76,)), (0x5df1a0, 0x24, (0x14,), (0x5df246,)),
                                         (0x5e0170, 0x20, (0x10, 0), (0x5e01fe,)))]


# -- skip lists with a string / vector value (skiplist_dicts.cpp, wip/watcom-r6) --
def skip_string_value(rng, pool, args):
    entry, _ = string_fields(rng, 0x7c, SCRATCH + SCRATCH_SLOT_BYTES)
    return skip_dict_count(rng, pool, args) + entry


def skip_vector_value(strings):
    def presets(rng, pool, args):
        count = rng.randrange(5)
        data = WORLD_BASE + SCRATCH if count or rng.random() < 0.5 else 0
        fields = skip_dict_count(rng, pool, args) + [(0x84, data, 4), (0x88, count, 4),
                                                     (0x8c, rng.randint(-1, count) & 0xFFFFFFFF, 4)]
        if strings and data:
            for i in range(count):
                entry, _ = string_fields(rng, SCRATCH + 16 * i, SCRATCH + SCRATCH_SLOT_BYTES + 0x100 * i)
                fields += entry
        return fields
    return presets


# Dead in the inlined ~string: the _Eos after erase(0, npos), and the count
# decrement of the release (the split before left the buffer unshared or
# frozen), as 0x40a6d0 / 0x40a6fc in StringVector_Resize.
SPECS += [
    Spec("SkipListDestructor_457a90", 0x00457a90, "thiscall", [], presets=skip_string_value,
         callees=SKIP_DESTROY_CALLEES, exempt_blocks=(0x00457b39, 0x00457b5a)),
    Spec("SkipListDestructor_499150", 0x00499150, "thiscall", [], presets=skip_string_value,
         callees=SKIP_DESTROY_CALLEES, exempt_blocks=(0x004991f9, 0x0049921a)),
    Spec("SkipListDestructor_457d90", 0x00457d90, "thiscall", [], presets=skip_vector_value(True),
         callees=SKIP_DESTROY_CALLEES),
    Spec("SkipListDestructor_472060", 0x00472060, "thiscall", [], presets=skip_vector_value(False),
         callees=SKIP_DESTROY_CALLEES),
    # cover_until: a static initializer follows inside Ghidra's function.
    Spec("SkipListDestructor_45d580", 0x0045d580, "thiscall", [], presets=skip_dict_count,
         callees=SKIP_DESTROY_CALLEES, cover_until=0x0045d5d4),
]
# 0x4583d0 destroys the vector at +4 of its object: ECX = world - 4.
SPECS += [Spec("StringVectorDestructor_4583d0", 0x004583d0, "fastcall", [one_of(WORLD_BASE - 4), anything],
               presets=string_vector_state, exempt_blocks=(0x00458476,))]  # the _Eos after erase


# -- the string dictionaries' base_new_node (skiplist_nodes.cpp) ----------
# The dictionary at world 0 (+0x5c: 0 or the allocator probe, which hands
# out ISV_BLOCK or null); the pair in scratch slot 2 (its string's buffer in
# slot 3); level 0..12.
def skip_node_world(rng, pool, args):
    entry, _ = string_fields(rng, SCRATCH + 2 * SCRATCH_SLOT_BYTES + 4, SCRATCH + 3 * SCRATCH_SLOT_BYTES)
    return [(0x5c, rng.choice([0, 0, ISV_VIRTUAL_CODE + ISV_ALLOC_SLOT]), 4)] + entry


SKIP_NODE_ARGS = [lambda rng, pool: WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES, lambda rng, pool: rng.randrange(13)]
# Dead in the inlined assign(x, 0, npos): the count clamp to npos (a length
# is at most npos) and the self-assignment path (the node is new). The empty
# string's count byte (0x65539f) is varied so that a source without buffer
# also takes the copy path.
SKIP_NODE_DEAD = {0x458e20: (0x458ea1, 0x458ea8, 0x458eb2, 0x458eb7, 0x458ec7, 0x458ec9, 0x458ecd, 0x458ef5, 0x458efd),
                  0x499b90: (0x499c11, 0x499c18, 0x499c22, 0x499c27, 0x499c37, 0x499c39, 0x499c3d, 0x499c65, 0x499c6d)}
SPECS += [Spec(f"SkipNewNode_{a:x}", a, "thiscall", SKIP_NODE_ARGS, presets=skip_node_world,
               virtuals=ISV_VIRTUALS, new_fails=0.2, globals=name_null_string_count, exempt_blocks=SKIP_NODE_DEAD[a])
          for a in (0x458e20, 0x499b90)]


# -- the sorted vectors' base_find_with_index (sorted_find.cpp) -----------
# The vector at world 0 with vtable FAKE_VTABLE (slots 2 and 4 probed with
# their 2 arguments; the probes' random results steer the search), its
# block in scratch slot 0, length 0..9 or odd; the element in slot 2; the
# index in slot 3 (random); mode 0..4.
def sorted_find_state(rng, pool, args):
    return [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH, 4),
            (0x14, rng.choice([0, 1, 2, 3, 5, 8, 9, rng.randrange(40), 0x80000000]), 4),
            (SCRATCH + 3 * SCRATCH_SLOT_BYTES, rng.choice([rng.randint(-2, 10), rng.getrandbits(32)]) & 0xFFFFFFFF, 4)]


SORTED_FIND_ARGS = [lambda rng, pool: WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES,
                    lambda rng, pool: rng.choice([0, 1, 2, 2, 3, 3, 4, rng.getrandbits(32)]),
                    lambda rng, pool: WORLD_BASE + SCRATCH + 3 * SCRATCH_SLOT_BYTES]
SPECS += [Spec(f"SortedFind_{a:x}", a, "thiscall", SORTED_FIND_ARGS, presets=sorted_find_state,
               virtuals={0x08: 2, 0x10: 2})
          for a in (0x437eb0, 0x44d330, 0x44d730, 0x44d850, 0x4585f0, 0x463840, 0x541790, 0x5bdd60, 0x5e0280)]


# -- more base_init_upto instantiations (vector_init.cpp, wip/watcom-r6) ----
# As InitUpTo_* above; the element constructors probed (with their 0
# argument(s) where they take them).
INIT_UPTO_R6 = ((0x462d70, 0x40, 0x5ba300, 0), (0x44dee0, 0x0c, 0x5dc810, 0), (0x44dc90, 0x20, 0x5dc1e0, 0),
                (0x44d960, 0x17c, 0x5dc4e0, 0), (0x44d460, 0x11c, 0x5da750, 0), (0x44d050, 0x54, 0x5d9e20, 0),
                (0x5deae0, 0x24, 0x5ec9f0, 2), (0x5de4f0, 0x18, 0x5ebfb0, 0), (0x60ad90, 0x24, 0x601370, 2),
                (0x541710, 0x18, 0x5ec1e0, 0), (0x60a0f0, 0x14, 0x601e40, 0), (0x5dff30, 0x20, 0x5c46b0, 0),
                (0x5bdc70, 0x08, 0x5bd200, 2), (0x5b82f0, 0x6c, 0x5b6c30, 1), (0x5b8040, 0x30, 0x5b6740, 0),
                (0x60a7e0, 0x0c, 0x6065f0, 1))
SPECS += [Spec(f"InitUpTo_{address:x}", address, "thiscall", [init_upto_n], presets=init_upto_state(size), eax_mask=0,
               callees={constructor: (args, "thiscall", "probe")})
          for address, size, constructor, args in INIT_UPTO_R6]
SPECS += [Spec("InitUpTo_5df410", 0x005df410, "thiscall", [init_upto_n], presets=init_upto_state(0x20), eax_mask=0)]


# -- the dictionaries' base_destroy_node (skiplist_nodes.cpp) -------------
# The dictionary at world 0 (+0x60: 0 or the allocator probe as the
# deallocator), the node in scratch slot 1 or null, level 0..12; the pair
# destructors probed.
def skip_destroy_node_world(rng, pool, args):
    return [(0x60, rng.choice([0, ISV_VIRTUAL_CODE + ISV_ALLOC_SLOT]), 4)]


SKIP_DESTROY_NODES = ((0x499d60, 0x49a070), (0x458ff0, 0x459440), (0x4591b0, 0x459550), (0x472dc0, 0x473290),
                      (0x60a760, 0x60b2d0))
SPECS += [Spec(f"SkipDestroyNode_{a:x}", a, "thiscall",
               [lambda rng, pool: rng.choice([0, WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 0x40]),
                lambda rng, pool: rng.randrange(13)],
               presets=skip_destroy_node_world, virtuals=ISV_VIRTUALS, callees={d: (0, "thiscall", "probe")})
          for a, d in SKIP_DESTROY_NODES]
SPECS += [Spec(f"OrderedFind_{a:x}", a, "thiscall", SORTED_FIND_ARGS, presets=sorted_find_state, virtuals={0x08: 2})
          for a in (0x5b83e0, 0x5b8290, 0x499b30, 0x468550, 0x40a220, 0x437c30, 0x44dc30, 0x462e60, 0x5de970)]


# -- wip/watcom-r6 batch 3 --------------------------------------------------
# More inline callbacks (vector_callbacks.cpp): a and b 0x30 into scratch
# slots 0 and 1 (keys up to 0x28 before them), on callback_buffers.
CALLBACK_ARGS_30 = [lambda rng, pool: WORLD_BASE + SCRATCH + 0x30,
                    lambda rng, pool: WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 0x30]
SPECS += [Spec(name, address, "stdcall", CALLBACK_ARGS_30, presets=callback_buffers)
          for name, address in (("SkipDictKeyEqual_6099e0", 0x6099e0), ("SkipDictKeyLess_609a00", 0x609a00),
                                ("SkipDictKeyEqual_4723c0", 0x4723c0), ("SkipDictKeyLess_4723e0", 0x4723e0),
                                ("BoolListEquivalent", 0x427f80), ("MessageListEquivalent", 0x427fa0),
                                ("IntVectorLessThan", 0x414180))]
SPECS += [Spec("MessageBlockVectorEquivalent", 0x00609ce0, "stdcall", CALLBACK_ARGS_30,
               callees={0x00601500: (0, "thiscall", "probe")})]

# The skip-list base and its derived constructors (skiplist_base.cpp):
# base_init and the base constructor (ours, own spec) probed.
SKIP_BASE_CALLEES = {0x0060bb80: (2, "thiscall", "probe"), 0x0060b9f0: (3, "thiscall", "probe")}
SPECS += [Spec("WCSkipNonTempBase_Construct", 0x0060b9f0, "thiscall", [anything, anything, anything],
               callees={0x0060bb80: (2, "thiscall", "probe")})]
SPECS += [Spec(f"SkipListConstructor_{a:x}", a, "thiscall", [anything, anything, anything], callees=SKIP_BASE_CALLEES)
          for a in (0x609f20, 0x609ed0, 0x458030)]
SPECS += [Spec("SkipDictConstructor_616020", 0x00616020, "thiscall", [anything, anything], callees=SKIP_BASE_CALLEES)]
SPECS += [Spec(f"SkipInsert_{a:x}", a, "thiscall", [anything], callees={0x0060bbe0: (1, "thiscall", "probe")})
          for a in (0x609b90, 0x472400)]

# List constructors (list_iterators.cpp). cover_until: a static initializer
# follows 0x53ea60 inside Ghidra's function.
SPECS += [Spec("IsvListConstructor_53ea60", 0x0053ea60, "thiscall", [], cover_until=0x0053ea74)]
SPECS += [Spec(f"SListConstructor_{a:x}", a, "thiscall", [anything, anything]) for a in (0x4639e0, 0x5b7d40, 0x5b7e00)]


# -- copy constructors (bare_vectors.cpp, string_vector.cpp; wip/watcom-r6) --
# The source is the vector at world 0 (plain_vector_state; its block in
# scratch slot 0), the copy in scratch slot 9.
SPECS += [Spec(f"VectorCopy_{a:x}", a, "fastcall",
               [one_of(WORLD_BASE + SCRATCH + 9 * SCRATCH_SLOT_BYTES), anything, one_of(WORLD_BASE)],
               presets=plain_vector_state, new_fails=0.1)
          for a in (0x5f3cc0, 0x5f3e40)]
# 0x4582b0 (key, vector): the key word in scratch slot 10, the vector as
# StringVector_CopyConstruct's source; the pair in scratch slot 9.
SPECS += [Spec("KeyedStringVector_Construct", 0x004582b0, "fastcall",
               [one_of(WORLD_BASE + SCRATCH + 9 * SCRATCH_SLOT_BYTES), anything,
                one_of(WORLD_BASE + SCRATCH + 10 * SCRATCH_SLOT_BYTES), one_of(WORLD_BASE)],
               presets=string_vector_state, new_fails=0.1)]


# -- wip/watcom-r6 batch 5: more vector constructors / destructors ---------
SPECS += [Spec("VectorDestructor_5bdbd0", 0x005bdbd0, "thiscall", [], presets=watcom_vector_to_destroy)]
SPECS += [Spec(f"VectorConstructor_{a:x}", a, "thiscall", [watcom_capacity, anything], new_fails=0.2)
          for a in (0x5dde80, 0x609ff0, 0x609e50, 0x60a070, 0x5ddf90, 0x5ddf10)]
SPECS += [Spec(f"BareVectorConstructor_{a:x}", a, "thiscall", [watcom_capacity], new_fails=0.2)
          for a in (0x5de330, 0x5df8d0, 0x5de570)]
SPECS += [Spec(f"ElementVectorDestructor_{a:x}", a, "thiscall", [], presets=element_vector_to_destroy(size),
               virtuals={0x00: 1})
          for a, size in ((0x609c40, 0x0c), (0x609d10, 0x24))]
# The pairs' destructors of a value at +4 (ECX = world - 4): 0x459550 as
# 0x4583d0 (dead: the _Eos after erase), 0x473290 a pointer vector.
SPECS += [Spec("StringVectorDestructor_459550", 0x00459550, "fastcall", [one_of(WORLD_BASE - 4), anything],
               presets=string_vector_state, exempt_blocks=(0x004595f6,)),
          Spec("VectorDestructor_473290", 0x00473290, "fastcall", [one_of(WORLD_BASE - 4), anything],
               presets=watcom_vector_to_destroy)]


# -- more base_new_node instantiations and 0x457c10 (skiplist_nodes2.cpp, libs-r7) --
# The dictionary at world 0 (+0x5c: no allocator or the allocator probe),
# the pair in scratch slot 2: the key, then (for the vector pairs) a vector
# whose block of 0..6 dwords or strings is in scratch slot 3 (capacity
# -1..8, filled -1..6, sometimes no block); level 0..12. CLMMessageBlock's
# copy constructor 0x601470 (ours, own spec) is probed; operator new fails
# in some cases.
NODES2_PAIR = SCRATCH + 2 * SCRATCH_SLOT_BYTES
NODES2_BLOCK = SCRATCH + 3 * SCRATCH_SLOT_BYTES
NODES2_TEXTS = SCRATCH + 4 * SCRATCH_SLOT_BYTES


def nodes2_world(strings):
    def presets(rng, pool, args):
        fields = [(0x5c, rng.choice([0, 0, ISV_VIRTUAL_CODE + ISV_ALLOC_SLOT]), 4)]
        filled = rng.randrange(7)
        capacity = rng.choice([filled, filled + 1, filled + 2, 0, 0xFFFFFFFF])
        fields += [(NODES2_PAIR + 4 + 0x0c, capacity, 4),
                   (NODES2_PAIR + 4 + 0x10, rng.choice([filled, filled, filled, 0xFFFFFFFF, rng.randrange(4)]), 4),
                   (NODES2_PAIR + 4 + 0x08, 0 if rng.random() < 0.1 else WORLD_BASE + NODES2_BLOCK, 4)]
        if strings:
            for k in range(7):
                entry, _ = string_fields(rng, NODES2_BLOCK + 16 * k, NODES2_TEXTS + 0x80 * k)
                fields += entry
        return fields
    return presets


NODES2_ARGS = [lambda rng, pool: WORLD_BASE + NODES2_PAIR, lambda rng, pool: rng.randrange(13)]
SPECS += [Spec(f"SkipNewNode_{a:x}", a, "thiscall", NODES2_ARGS, presets=nodes2_world(False), virtuals=ISV_VIRTUALS,
               new_fails=0.2)
          for a in (0x472e60, 0x472c70)]
SPECS += [Spec("SkipNewNode_60a6d0", 0x0060a6d0, "thiscall", NODES2_ARGS, presets=nodes2_world(False),
               virtuals=ISV_VIRTUALS, new_fails=0.2, callees={0x00601470: (1, "thiscall", "probe")})]
SPECS += [Spec("SkipNewNode_459030", 0x00459030, "thiscall", NODES2_ARGS, presets=nodes2_world(True),
               virtuals=ISV_VIRTUALS, new_fails=0.2, globals=name_null_string_count)]
SPECS += [Spec("SkipDictConstructor_457c10", 0x00457c10, "thiscall", [anything, anything], callees=SKIP_BASE_CALLEES,
               new_fails=0.2)]


# -- base_copy 0x4732f0, the request-table iterator value() 0x472ef0 and the
# message-block dictionary copy 0x609a20 (skiplist_nodes2.cpp, libs-r7) ----
# The source vector in scratch slot 1 (+4 after a key), its block of up to 9
# pointers in slot 2 (capacity -1..9, filled <= capacity, sometimes no
# block); value() runs on an iterator at world 0 whose +0xc is null or the
# source's node, into a result in slot 3; its callees (ours) run.
COPY_PAIR = SCRATCH + SCRATCH_SLOT_BYTES
COPY_BLOCK = SCRATCH + 2 * SCRATCH_SLOT_BYTES


def copy_source(rng):
    capacity = rng.choice([0, 0xFFFFFFFF, rng.randint(1, 9), rng.randint(1, 9), rng.randint(1, 9)])
    filled = rng.randint(-1, capacity) if capacity <= 9 else rng.randint(-1, 2)
    data = 0 if capacity > 9 or capacity == 0 and rng.random() < 0.5 else WORLD_BASE + COPY_BLOCK
    return [(COPY_PAIR + 4 + 0x0c, capacity, 4), (COPY_PAIR + 4 + 0x10, filled & 0xFFFFFFFF, 4),
            (COPY_PAIR + 4 + 0x08, data, 4)]


def copy_presets(rng, pool, args):
    return copy_source(rng)


def iter_value_presets(rng, pool, args):
    return copy_source(rng) + [(0x0c, rng.choice([0, WORLD_BASE + COPY_PAIR + 0x20, WORLD_BASE + COPY_PAIR + 0x20]), 4)]


SPECS += [
    Spec("VectorBaseCopy_4732f0", 0x004732f0, "thiscall", [one_of(WORLD_BASE + COPY_PAIR + 4)], presets=copy_presets,
         eax_mask=0, new_fails=0.2),
    Spec("SkipIterValue_472ef0", 0x00472ef0, "thiscall", [one_of(WORLD_BASE + SCRATCH + 3 * SCRATCH_SLOT_BYTES)],
         presets=iter_value_presets, new_fails=0.2),
    Spec("SkipDictCopy_609a20", 0x00609a20, "thiscall", [one_of(WORLD_BASE + COPY_PAIR)],
         callees={0x0060b9f0: (3, "thiscall", "this"), 0x00601370: (2, "thiscall", "this"),
                  0x0060ba60: (1, "thiscall", "probe")}),
]
