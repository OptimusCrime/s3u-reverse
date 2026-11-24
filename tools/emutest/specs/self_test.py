"""Emulator specs, part `self_test`: SELF_TEST: deliberately wrong pairings the harness must catch.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""

# Deliberately wrong pairings for `run.py --self-test`: each must be reported
# as a mismatch, which shows that the harness notices wrong return values,
# wrong memory writes and wrong conditions.
SELF_TEST += [
    Spec("NeighbourX", 0x00503730, "stdcall", [anything, below(8)]),
    Spec("CGameData_CountBuildingsWithFlag21Clear", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("CGameData_SetBuildingFlag04", 0x004df690, "thiscall", [between(1, 2000)], eax_mask=0,
         presets=building_request_state),
    Spec("CGameData_IsOnMap", 0x00519510, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("CGameData_ClearBuildingFlag08", 0x004df490, "thiscall", [between(1, 2000)],
         eax_mask=0, presets=building_request_state),
    Spec("CGameData_SetAreaBuildingSlotA", 0x004de040, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    Spec("CGameData_ScreenCenterTileX", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("CGameData_FindFreeId4096", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("CGameData_ClearDiagonalTileByteB", 0x00518d10, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=100),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CGameData_FloodFillTileByte10", 0x0051c6c0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x16, 2), max_cases=100),
    # ~string erases first, so even on an unshared buffer it writes before freeing.
    Spec("String_TidyFree", 0x00401b60, "thiscall", [], eax_mask=0, presets=string_world),
    # The same destructor shape on a member at another offset (+0x8, not +0x14).
    Spec("DestroyStringMember14_411980", 0x00441740, "thiscall", [], eax_mask=0,
         presets=member_strings((0x08, 0x14))),
    # Virtual-call probes: the same parent call, but slot 0x1c instead of 0x78.
    ui_spec("CScreenElement_Invalidate", 0x004160d0, [], eax_mask=0),
    # API probes: the clip is intersected with the element (PtInRect is not called).
    ui_spec("CScreenElement_DrawClipped", 0x00415af0, [anything, one_of(UI_RECTS + 16)], eax_mask=0),
    # ignore_bytes: 0x5439a0 and 0x543e80 differ only in bytes +0x38 / +0x39,
    # next to the ignored padding byte +0x3b.
    make_effect("543e80", 7, original=0x005439a0),
    # COM probes: Blt (slot 0x14) against BltFast (slot 0x1c).
    surface_spec("DirectX_CSurface_DDBlt", 0x005bf130,
                 [anything, one_of(SOURCE_SURFACE), one_of(SOURCE_SURFACE), anything, anything]),
    # Callee probes and _CxxThrowException: the DirectPlay check throws for
    # success codes too, with another constructor and throw info.
    Spec("DirectX_ThrowIfDDFailed", 0x005bb890, "stdcall", [hresult, anything], callees=EXCEPTION_CTORS),
]

SELF_TEST += [
    # API probe CloseHandle against operator delete of +4 (and +0xc cleared).
    Spec("Lobby_FreeBlock_601250", 0x0060bf20, "thiscall", [], presets=handle_world),
    # ignore_bytes on the effect table: the two spawn functions differ only
    # in the stationary record's kind (dword +0x34: 0x63 against 0x5d), a few
    # bytes from the ignored padding bytes +0x3b / +0x3e.
    spawn_spec("MapEffects_Spawn_545070", 0x00544f20, [anything] + [coordinate] * 4),
]

SELF_TEST += [
    # ignore_bytes on the result's allocator byte: the wrong text function
    # drops the newline, so the length dword (+8) and the text differ.
    Spec("WrongDescribe", 0x00612e80, "thiscall", [one_of(STRING_RESULT_PTR)],
         presets=assertment_object, ignore_bytes=(STRING_RESULT,)),
]
