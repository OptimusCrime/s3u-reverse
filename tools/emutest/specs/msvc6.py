"""Emulator specs, part `msvc6`: specs of the replacements in src/s3re/msvc6/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


def string_count(rng, pool) -> int:
    """Lengths, positions and counts: small, around the 31-character minimum
    capacity, npos and its neighbours (too long: std::_Xlen), or large
    (beyond the fake heap, so an allocation returns null and the copy
    faults at once instead of crawling through megabytes)."""
    roll = rng.random()
    if roll < 0.55:
        return rng.randrange(0x62)
    if roll < 0.7:
        return rng.choice([0, 1, 0x1f, 0x20])
    if roll < 0.85:
        # Not 0xFFFFFFFD (nor anything from 0x7FFFFFFE up): valid for
        # _Grow, but _Copy's size turns negative, it allocates 0 bytes and
        # the copy crawls through the emulated world before it faults.
        return rng.choice([0xFFFFFFFF, 0xFFFFFFFE])
    return (rng.getrandbits(31) | 0x10000000)  # positive as an allocation size


def copy_count(rng, pool) -> int:
    """For _Copy alone, which copies only the current text: also sizes whose
    allocation size turns negative (the allocator then asks for 0 bytes)."""
    if rng.random() < 0.1:
        return rng.choice([0x7FFFFFFE, 0xFFFFFFFD, 0x80000000 | rng.getrandbits(31)])
    return string_count(rng, pool)

SPECS += [
    Spec("String_Eos", 0x00401f60, "thiscall", [below(0x60)], eax_mask=0, presets=string_world),
    Spec("String_Tidy", 0x00402170, "thiscall", [string_bool], eax_mask=0, presets=string_world),
    Spec("String_TidyFree", 0x00401d20, "thiscall", [], eax_mask=0, presets=string_world),
    Spec("String_MaxSize", 0x00402260, "thiscall", []),
    # _Copy continues at 0x4022f0 (a separate Ghidra function); its catch
    # handler (0x4022c9..0x4022ef) only runs if operator new throws.
    Spec("String_Copy", 0x00402270, "thiscall", [copy_count], eax_mask=0, presets=string_world,
         cover_until=0x00402380, exempt_blocks=(0x004022c9, 0x004022d6, 0x004022d8, 0x004022ea)),
    # Dead: 0x401fe4 and 0x402046 decrement a count that the inlined _Tidy
    # re-checks after _Grow already found it to be 0 or 0xFF.
    Spec("String_Grow", 0x00401f80, "thiscall", [string_count, string_bool], eax_mask=0xFF,
         presets=string_world, exempt_blocks=(0x00401fe4, 0x00402046)),
    # Dead: 0x4020e9 is _Xlen for a strlen above 0xFFFFFFFD; 0x4020f7..0x402106
    # re-check the buffer pointer that the inlined _Tidy just set to null.
    Spec("String_Split", 0x004020a0, "thiscall", [], eax_mask=0, presets=string_world,
         exempt_blocks=(0x004020e9, 0x004020f7, 0x004020fe, 0x00402102, 0x00402106)),
    Spec("String_AssignChars", 0x004021c0, "thiscall", [one_of(STRING_TEXT_PTR), string_count],
         presets=string_world),
    # Dead: 0x401da0 (_Xran in erase(pos + n) of a self-assignment: pos + n
    # <= length by construction) and 0x401e01 (the second erase finds a
    # length below pos: the first erase leaves at least pos characters).
    Spec("String_AssignSubstr", 0x00401d60, "thiscall",
         [one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), string_count, string_count],
         presets=string_world, exempt_blocks=(0x00401da0, 0x00401e01)),
    # Dead: erase() removes everything, so its _Grow(0) is false (0x401c19),
    # and after _Split the buffer is unshared, so the final release never
    # just decrements (0x401c3a).
    Spec("String_Destroy", 0x00401b60, "thiscall", [], eax_mask=0, presets=string_world,
         exempt_blocks=(0x00401c19, 0x00401c3a)),
    # Dead: 0x401b07 frees the second temporary, which `this` always shares
    # (it is non-empty with a fresh count 0, or empty without a buffer).
    Spec("String_FromChars", 0x00401a10, "thiscall", [text_or_null, string_count],
         presets=string_world, exempt_blocks=(0x00401b07,)),
    Spec("String_Default", 0x00407880, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world),
    # Dead: 0x40248a (PlusChars) and 0x401ce6 (ConcatChars) free the
    # temporary sum, which the result always shares (same reason as above).
    # Unreachable in practice: 0x4023ee (_Xlen in append) needs a string of
    # about 4 GB.
    Spec("String_PlusChars", 0x00402380, "stdcall",
         [one_of(STRING_RESULT_PTR), one_of(STRING_SELF_PTR, STRING_OTHER_PTR), one_of(STRING_TEXT_PTR)],
         presets=string_world, exempt_blocks=(0x0040248a, 0x004023ee)),
    Spec("String_ConcatChars", 0x00401c70, "thiscall", [text_or_null], presets=string_world,
         edx=one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), exempt_blocks=(0x00401ce6,)),
    # Batch 27: the byte-identical twin in Dirk\GameParams.cpp's unit.
    Spec("String_ConcatChars2", 0x00437480, "thiscall", [text_or_null], presets=string_world,
         edx=one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), exempt_blocks=(0x004374f6,)),
]


def error_objects(rng, pool, args):
    """std::logic_error objects (exception: +4 what, +8 free flag; message
    string at +0xc) at `this` and at STRING_OTHER."""
    fields = member_strings((0xc,), (0xc,))(rng, pool, args)
    for base in (STRING_SELF, STRING_OTHER):
        fields.append((base + 4, rng.choice([STRING_TEXT_PTR, STRING_TEXT_PTR, 0x00655390]), 4))
        fields.append((base + 8, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4))
    return fields


# Dead blocks shared by many of these (the reasons, referred to below):
#   DROP: deleting the temporary the result was built from; the result
#     always shares its buffer (non-empty with a fresh count) or it has none.
#   XLEN: std::_Xlen for a strlen (or a length minus a removed part) above
#     0xFFFFFFFD, which needs a string of about 4 GB.
#   CLEARED: the shared-buffer branch of an inlined _Grow on a string that a
#     constructor has just emptied (no buffer).
#   UNSHARED: the not-shared branches of an inlined _Grow after the code
#     found the buffer shared (or the shared branch after _Split).
#   DESTROY: the inlined ~string: erase() removes everything, so its
#     _Grow(0) is false (no _Eos), and after _Split the buffer is unshared,
#     so the release never just decrements.
#   NPOS: `count = npos` when npos < the source length (never: lengths are
#     at most 0xFFFFFFFF = npos), and the self-assignment's _Xran (the count
#     never exceeds the length).
SPECS += [
    Spec("String_FromSubstr", 0x004078a0, "thiscall",
         [SELF_OR_OTHER, string_count, string_count, one_of(STRING_TEXT_PTR)], presets=string_world,
         # Dead: the self-assignment path on a just-emptied string (pos and
         # count are 0: no _Xran, nothing to move or terminate) and the
         # release of its (null) old buffer before sharing.
         exempt_blocks=(0x004078f4, 0x0040790f, 0x00407938, 0x00407950, 0x0040795a, 0x00407984,
                        0x004079b8, 0x004079c2, 0x004079c6, 0x004079cc)),
    Spec("String_FromText", 0x00407a50, "thiscall", [one_of(STRING_TEXT_PTR), one_of(STRING_TEXT_PTR)],
         presets=string_world,  # XLEN 0x407a7f, CLEARED 0x407a8b..0x407a9a
         exempt_blocks=(0x00407a7f, 0x00407a8b, 0x00407a92, 0x00407a96, 0x00407a9a)),
    Spec("String_AssignRepeat", 0x0041f430, "thiscall", [string_count, anything], presets=string_world),
    Spec("String_FromRepeat", 0x005f3bf0, "thiscall", [string_count, anything, one_of(STRING_TEXT_PTR)],
         presets=string_world,  # CLEARED
         exempt_blocks=(0x005f3c2a, 0x005f3c31, 0x005f3c35, 0x005f3c39)),
    Spec("String_Erase", 0x00407b10, "thiscall", [string_count, string_count], presets=string_world,
         exempt_blocks=(0x00407bbf, 0x00407bd6, 0x00407bda)),  # XLEN, UNSHARED (after _Split)
    Spec("String_AssignText", 0x00409a90, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world,
         exempt_blocks=(0x00409aad,)),  # XLEN
    Spec("String_AssignTextOrEmpty", 0x00413d70, "thiscall", [text_or_null], presets=string_world,
         globals=empty_text, exempt_blocks=(0x00413e2b, 0x00413e4f)),  # DROP, XLEN
    Spec("String_Freeze", 0x00414490, "thiscall", [], eax_mask=0, presets=string_world,
         # XLEN (a shared 4 GB string), UNSHARED
         exempt_blocks=(0x004144ae, 0x004144d9, 0x004144dd, 0x004144e1, 0x004144eb)),
    Spec("String_At", 0x004142d0, "thiscall", [string_count], presets=string_world,
         exempt_blocks=(0x004142fa, 0x00414325, 0x00414329, 0x0041432d, 0x00414334)),  # as _Freeze
    Spec("String_PlusString", 0x00409e60, "stdcall",
         [one_of(STRING_RESULT_PTR), SELF_OR_OTHER, SELF_OR_OTHER], presets=string_world,
         exempt_blocks=(0x00409f71,)),  # DROP
    Spec("String_CharsPlus", 0x00409f90, "stdcall",
         [one_of(STRING_RESULT_PTR), one_of(STRING_TEXT_PTR), SELF_OR_OTHER], presets=string_world,
         exempt_blocks=(0x0040a0ba,)),  # DROP
    Spec("String_PlusChar", 0x00421aa0, "stdcall",
         [one_of(STRING_RESULT_PTR), SELF_OR_OTHER, anything], presets=string_world,
         # XLEN (append to a 4 GB string), DROP (0x421b86 and the exit after it;
         # the sum is never empty)
         exempt_blocks=(0x00421afa, 0x00421b86, 0x00421b8f)),
    Spec("String_ConcatString", 0x004077f0, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=SELF_OR_OTHER, exempt_blocks=(0x00407862,)),  # DROP
    Spec("String_ConcatCharsLeft", 0x00408e50, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=text_or_null, exempt_blocks=(0x00408ec6,)),  # DROP
    Spec("String_ConcatCharsLeft2", 0x0043d8e0, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=text_or_null, exempt_blocks=(0x0043d956,)),  # DROP
    Spec("String_ConcatChar", 0x00420cb0, "thiscall", [anything], presets=string_world,
         edx=SELF_OR_OTHER, exempt_blocks=(0x00420d22,)),  # DROP
]

SPECS += [
    Spec("LogicError_What", 0x00559630, "thiscall", [], presets=error_objects),
    Spec("LogicError_Destroy", 0x005597e0, "thiscall", [], eax_mask=0, presets=error_objects),
    Spec("LogicError_DeletingDestructor", 0x005597c0, "thiscall", [string_bool], presets=error_objects),
    # Dead: 0x559694 (NPOS) and, in the copy onto itself, _Xran, the move
    # and _Eos of the inlined erase(): the message was just emptied.
    Spec("LogicError_Copy", 0x00559640, "thiscall", [SELF_OR_OTHER], presets=error_objects,
         globals=null_string_count, exempt_blocks=(0x00559694, 0x005596a0, 0x005596bb, 0x005596e3)),
    Spec("OutOfRange_Copy", 0x00559850, "thiscall", [SELF_OR_OTHER], presets=error_objects,
         globals=null_string_count, exempt_blocks=(0x005598a3, 0x005598af, 0x005598ca, 0x005598f2)),
]

# -- batch 25: std::string compare (msvc6/string.cpp) -----------------------


def compare_world(rng, pool, args):
    """`this` = a string at world+0 over a buffer of small letters (or
    empty with a null pointer); the other text at scratch 1."""
    length = rng.randrange(24)
    null = length == 0 and rng.random() < 0.5
    fields = [(4, 0 if null else WORLD_BASE + SCRATCH, 4), (8, length, 4)]
    letters = [rng.choice(b"ab\x80\xff") for _ in range(32)]
    other = list(letters) if rng.random() < 0.5 else [rng.choice(b"ab\x80\xff") for _ in range(32)]
    for i in range(32):
        fields.append((SCRATCH + i, letters[i], 1))
        fields.append((SCRATCH + SCRATCH_SLOT_BYTES + i, other[i], 1))
    return fields

SPECS += [
    Spec("String_CompareChars", 0x0040a140, "thiscall",
         [below(26), one_of(0, 1, 3, 30, 0xFFFFFFFF), scratch(1), below(26)], presets=compare_world),
]

SPECS += [
    # Dead, as at String_FromSubstr: the self-assignment path on a
    # just-emptied string (_Xran, the memmove, _Eos, the second _Xran
    # check, the copy and the release of its null old buffer).
    # globals: the empty string's count byte is set per case; a value of
    # 0xfe or more left behind by an earlier spec in a full run otherwise
    # hides the sharing path (0x408dde).
    Spec("String_FromSubstrDefault", 0x00408cc0, "thiscall", [SELF_OR_OTHER, string_count, string_count],
         presets=string_world, globals=null_string_count,
         exempt_blocks=(0x00408d14, 0x00408d2f, 0x00408d57, 0x00408d6f, 0x00408d79, 0x00408da3)),
]

SPECS += [
    Spec("String_CompareText", 0x0040a0e0, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world),
]


# -- Compiler-generated deleting destructors (deleting_destructors.cpp, session 56) --
#
# The class destructors and ~CScreenElement 0x4156b0 are probed (their
# calls are what these functions do; they have their own specs). The
# virtual-base kinds get ECX = the virtual base (world + offset), so the
# object starts at world offset 0.

DELETING_DESTRUCTORS = (  # (address, class destructor, virtual-base offset or None)
    (0x44e300, 0x401b60, None), (0x44e320, 0x441740, None), (0x4639c0, 0x45f060, None),
    (0x46db20, 0x49d980, None), (0x41d990, 0x41d9b0, None), (0x4993d0, 0x499620, None),
    (0x49cb50, 0x434680, None), (0x4a5b20, 0x4a5b50, None),
    (0x42a710, 0x42a750, 0x98), (0x42af60, 0x421fc0, 0x444), (0x42b340, 0x42b380, 0x66c),
    (0x49b610, 0x49b650, 0x914),
    # the Watcom vectors' and Dirk::CTeam's
    (0x40a1b0, 0x409d50, None), (0x414390, 0x4143b0, None), (0x4281f0, 0x427de0, None),
    (0x428210, 0x427e40, None), (0x4375d0, 0x4141a0, None), (0x437610, 0x437570, None),
    (0x44cd70, 0x44c980, None), (0x44cd90, 0x44caa0, None), (0x44cdf0, 0x44cca0, None),
    (0x457d10, 0x457a10, None), (0x462b60, 0x462870, None), (0x462c50, 0x462b00, None),
    (0x4724e0, 0x4721c0, None), (0x472500, 0x472240, None), (0x472520, 0x4722e0, None),
    (0x472540, 0x472360, None), (0x4726c0, 0x472440, None), (0x499350, 0x4990c0, None),
    (0x4993b0, 0x4992f0, None), (0x414b90, 0x4a04d0, None),
    (0x4375f0, 0x414240, None),
    # session 57: the UI range's (plain, virtual-base, and "sub" with this = object + 4)
    (0x41c560, 0x41c580, None), (0x41de80, 0x41dea0, None), (0x428230, 0x427ea0, None),
    (0x428250, 0x427f10, None), (0x428270, 0x428290, None), (0x428320, 0x427fe0, None),
    (0x428340, 0x428070, None), (0x428360, 0x428120, None), (0x428380, 0x4283a0, None),
    (0x429730, 0x4275e0, None), (0x429c30, 0x429c70, 0x10c), (0x42a060, 0x42a0a0, 0xc4),
    (0x42a250, 0x42a280, 0x68), (0x42a340, 0x42a380, 0x434), (0x42a630, 0x416da0, 0x80),
    (0x42a8c0, 0x4180a0, 0x3c), (0x42a980, 0x41a900, 0x50), (0x42aa60, 0x41b630, 0x60),
    (0x42ab40, 0x42ab70, 0x74), (0x42ad20, 0x421f50, 0x70), (0x42ae10, 0x41e210, 0xb8),
    (0x42b0c0, 0x42b100, 0x224), (0x42b600, 0x423870, 0x59c), (0x42b770, 0x41f010, 0x2f4),
    (0x42c310, 0x42c340, 0x4), (0x42c9b0, 0x42ca00, None), (0x430440, 0x430460, None),
    (0x4308b0, 0x4308e0, 0x4), (0x457d30, 0x457a90, None), (0x457d50, 0x457bb0, None),
    (0x457d70, 0x457d90, None), (0x457e80, 0x457cb0, None), (0x45ad40, 0x45ad60, None),
    (0x45d4e0, 0x45d500, None), (0x45d560, 0x45d580, None), (0x462b80, 0x4628f0, None),
    (0x462ba0, 0x462bc0, None), (0x4639a0, 0x463020, None), (0x469050, 0x469070, None),
    (0x497e20, 0x497e40, None), (0x499370, 0x499150, None), (0x499390, 0x499290, None),
    (0x49a190, 0x49a1d0, 0x158), (0x49a4b0, 0x492620, 0x1a8), (0x49a630, 0x492c50, 0xf4),
    (0x49a7a0, 0x49a7e0, 0x96c), (0x49ab60, 0x486950, 0x32e4), (0x49acd0, 0x49ad10, 0xbe0),
    (0x49b180, 0x49b1c0, 0xaa4), (0x49b4a0, 0x4797e0, 0xaec), (0x49b9d0, 0x4816f0, 0x269c),
    (0x49bb40, 0x492270, 0x73c), (0x49bcb0, 0x49bcf0, 0xcfc), (0x49da90, 0x49dab0, None),
    (0x4a6c00, 0x4a6a40, None), (0x4a6c20, 0x4a6c40, None), (0x4a6ca0, 0x4a6b40, None),
    (0x4a6cc0, 0x4a6ce0, None), (0x4a7840, 0x4a7860, None), (0x4a8e70, 0x4a8e10, None),
    (0x4a8e90, 0x4a8eb0, None),)
SCREEN_ELEMENT_DESTRUCTOR = 0x4156b0


def deleting_destructor_spec(address, destructor, offset):
    callees = {destructor: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}
    if offset is None:
        return Spec(f"ScalarDeletingDestructor_{address:x}", address, "thiscall", [below(4)], callees=callees)
    return Spec(f"ScalarDeletingDestructor_{address:x}", address, "fastcall",
                [one_of(WORLD_BASE + offset), anything, below(4)], callees=callees)


SPECS += [deleting_destructor_spec(*entry) for entry in DELETING_DESTRUCTORS]
SPECS += [
    Spec("ScalarDeletingDestructor_405360", 0x00405360, "thiscall", [below(4)]),
    Spec("ScalarDeletingDestructor_4281d0", 0x004281d0, "thiscall", [below(4)]),
    Spec("CompleteDestructor_421fa0", 0x00421fa0, "thiscall", [],
         callees={0x421fc0: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_4816d0", 0x004816d0, "thiscall", [],
         callees={0x42b380: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_41e270", 0x0041e270, "thiscall", [],
         callees={0x41b630: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_41eff0", 0x0041eff0, "thiscall", [],
         callees={0x41f010: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_421f30", 0x00421f30, "thiscall", [],
         callees={0x421f50: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_423850", 0x00423850, "thiscall", [],
         callees={0x416da0: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_439520", 0x00439520, "thiscall", [],
         callees={0x42ab70: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_4416e0", 0x004416e0, "thiscall", [],
         callees={0x42a380: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_441700", 0x00441700, "thiscall", [],
         callees={0x423870: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_441720", 0x00441720, "thiscall", [],
         callees={0x42a280: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_476ef0", 0x00476ef0, "thiscall", [],
         callees={0x41a900: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
    Spec("CompleteDestructor_486930", 0x00486930, "thiscall", [],
         callees={0x42b100: (0, "thiscall", "probe"), SCREEN_ELEMENT_DESTRUCTOR: (0, "thiscall", "probe")}),
]


# -- operator[] const and append(x, pos, n) (string.cpp, session 56) --------
SPECS += [
    Spec("String_IndexConst", 0x00414370, "thiscall", [string_count], presets=string_world),
    # XLEN 0x42f96c (the inlined _Grow's max_size check: needs a result of
    # exactly 0xFFFFFFFE characters); DEAD 0x42f987, 0x42f9a1, 0x42f9a5 (the
    # inlined _Grow's new-length-0 branches: the count is > 0 there and the
    # npos check keeps len + count from wrapping).
    Spec("String_AppendSubstr", 0x0042f920, "thiscall", [SELF_OR_OTHER, string_count, string_count],
         presets=string_world, exempt_blocks=(0x0042f96c, 0x0042f987, 0x0042f9a1, 0x0042f9a5)),
]


# -- std::wstring _Tidy / compare, allocator<char>::deallocate (wstring.cpp, session 56) --
# The wide string at world offset 0 with its block in scratch slot 0 (count
# byte at +1, ptr = +2) or none. _Tidy is void in the library (EAX is left over).


def wstring_world(rng, pool, args):
    ptr = rng.choice([0, WORLD_BASE + SCRATCH + 2, WORLD_BASE + SCRATCH + 2, WORLD_BASE + SCRATCH + 2])
    length = rng.randrange(40) if ptr else 0
    fields = [(4, ptr, 4), (8, length, 4), (SCRATCH + 1, rng.choice([0, 1, 2, 0xfe, 0xff]), 1)]
    text = [rng.choice([0x61, 0x62, 0x8061, rng.getrandbits(16)]) for _ in range(44)]
    other = list(text) if rng.random() < 0.5 else [rng.choice([0x61, 0x62, rng.getrandbits(16)]) for _ in range(44)]
    fields += [(SCRATCH + 2 + 2 * k, v, 2) for k, v in enumerate(text)]
    fields += [(SCRATCH + SCRATCH_SLOT_BYTES + 2 * k, v, 2) for k, v in enumerate(other)]
    return fields


SPECS += [
    Spec("WString_Tidy", 0x004286d0, "thiscall", [lambda rng, pool: rng.randrange(2) | (rng.getrandbits(24) << 8)],
         presets=wstring_world, eax_mask=0),
    Spec("WString_CompareChars", 0x00429980, "thiscall",
         [lambda rng, pool: rng.randrange(42), lambda rng, pool: rng.choice([0xFFFFFFFF, rng.randrange(44)]),
          lambda rng, pool: WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES + 2 * rng.randrange(4),
          lambda rng, pool: rng.randrange(40)], presets=wstring_world),
    Spec("Allocator_Deallocate", 0x00486920, "thiscall", [anything, anything]),
]
