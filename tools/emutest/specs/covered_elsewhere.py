"""Emulator specs, part `covered_elsewhere`: COVERED_ELSEWHERE: blocks whose evidence is elsewhere.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""


# Blocks of a spec's original that its emulator inputs don't reach in
# reasonable time, with the evidence for them: {spec: (blocks, reason)}.
# run.py reports them as covered elsewhere (not a --require-coverage
# failure). Only for blocks measured as unreached by a --full run; not part
# of the spec entries, so editing it doesn't reselect specs.
COVERED_ELSEWHERE = {
    # CLandscapeSurface::CLandscapeSurface after the 1,000,000-byte table
    # (+0x9c), which the emulator's heap (0x30000 bytes) can't hold: every
    # case ends in that out-of-memory throw (wip/logic-r4). Evidence: the
    # in-process CLandscapeSurface_Construct_MatchesOriginal (a fake operator
    # new, every combination of failing allocations).
    "CLandscapeSurface_Construct": ((0x54bdd7, 0x54bdee, 0x54bdff, 0x54be16, 0x54be27),
                                    "in-process: CLandscapeSurface_Construct_MatchesOriginal"),
    # CLogicThread::SaveGame after the allocation of chunk 10's 0x8d63a-byte
    # buffer, which the emulator's heap (0x30000 bytes) can't hold: every
    # case ends in that out-of-memory throw (2026-11-25). Evidence: the
    # in-process tests CLogicThread_SaveGame_AllSaveGames, _Maps and
    # _RandomWorlds_MatchOriginal (whole files written by the original and
    # ours from the same worlds, byte for byte outside the bytes neither
    # writes; computer players, both results of GetError).
    "CLogicThread_SaveGame": ((
        0x50a6cc, 0x50a770, 0x50a784, 0x50a7eb, 0x50a7ff, 0x50a870, 0x50a884, 0x50a8f6, 0x50a90a, 0x50a972,
        0x50a986, 0x50a9e9, 0x50a9ef, 0x50a9f9, 0x50a9ff, 0x50aa0f, 0x50aa23, 0x50aa72, 0x50aa86, 0x50aace,
        0x50aae2, 0x50ab2c, 0x50ab40, 0x50ab8d, 0x50aba1, 0x50abe6, 0x50abeb, 0x50ac01, 0x50ac06),
        "in-process: CLogicThread_SaveGame_AllSaveGames, _Maps, _RandomWorlds_MatchOriginal"),
    # The 77 blocks a --full run (10 maps of 33..96 tiles, Release, 146 s)
    # doesn't reach, 2026-11-24. Many need maps of 130+ tiles (0x516815: a
    # tile 65 or more from every border) or 258+ (0x516842: 129 or more);
    # the others depend on what larger maps contain. Measured: 3 extra maps
    # of 192 tiles took 220 s each in Release and reached only 7 of them
    # (0x516815, 0x517424, 0x517443, 0x517446, 0x5177db, 0x517cc5,
    # 0x517cd5), so no larger case was added. Evidence for them: the
    # in-process whole-world comparisons of world_create.cpp on maps of 128
    # to 768 tiles and the random-map whole-game checks (check_random_map.py).
    "CGameData_GenerateRandomMap": ((
        0x516815, 0x51681c, 0x516821, 0x516825, 0x51682d, 0x516836, 0x51683e, 0x516842,
        0x516867, 0x516ad7, 0x516bf7, 0x516c41, 0x516e14, 0x516e2d, 0x516ee1, 0x516ee9,
        0x516ef2, 0x516f25, 0x516f2e, 0x516f47, 0x516f79, 0x516f7e, 0x516f93, 0x516f9b,
        0x516fa4, 0x516fc9, 0x516fd2, 0x516ffc, 0x517015, 0x517033, 0x51705d, 0x517076,
        0x5170ae, 0x5170e0, 0x5170e5, 0x5170f7, 0x5170ff, 0x517108, 0x517151, 0x51715a,
        0x51716f, 0x51719a, 0x51719f, 0x5171b1, 0x5171b5, 0x5171ba, 0x5171ed, 0x5171fb,
        0x517200, 0x517225, 0x51723a, 0x5172e8, 0x5172f1, 0x51738b, 0x517395, 0x51739e,
        0x5173a7, 0x5173b8, 0x5173c3, 0x5173e4, 0x5173ed, 0x5173f3, 0x5173fc, 0x517411,
        0x517424, 0x517443, 0x517446, 0x5175f2, 0x5177db, 0x51793a, 0x517cc5, 0x517cd5,
        0x517cfe, 0x517df7, 0x517e15, 0x518c43, 0x518c8d,
    ), "maps of 128+ tiles: world_create.cpp in-process tests, check_random_map.py"),
}
