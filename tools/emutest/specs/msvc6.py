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
