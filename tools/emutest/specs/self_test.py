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

SELF_TEST += [
    # Thrower callees ("throw"): Write against Read, which throws with
    # another line (0x3c against 0x44) when the call fails.
    Spec("Win32_CFile_Write", 0x006198d0, "thiscall", [anything, anything, out_or_null, anything],
         callees=WIN32_THROWERS),
]

SELF_TEST += [
    # ignore_bytes on CGraphicsData's name allocator byte (+0x24): the wrong
    # constructor sets the name's length (+0x28) to 1.
    Spec("WrongGfxConstruct", 0x005f67c0, "thiscall", [], ignore_bytes=(0x24,)),
    # ignore_bytes on FindDirectoryContainingFile's result allocator byte:
    # the wrong search drops the trailing backslash (length at +8 differs).
    Spec("WrongFindDirectory", 0x005f6a10, "thiscall",
         [one_of(WORLD_BASE + GFX_RESULT), one_of(WORLD_BASE + GFX_FOLDERS), one_of(WORLD_BASE + GFX_FILE)],
         presets=gfx_folders, callees=GFX_THROWERS, ignore_bytes=(GFX_RESULT,), cover_until=0x005f6fc5),
    # ignore_bytes on bytes 5..7 of SiteTree::insert's result pair: the wrong
    # insert reports "not inserted" (byte 4) for the first node of a tree.
    Spec("WrongSiteTreeInsertFlag", 0x00559270, "thiscall", [scratch(11), scratch(12)],
         presets=site_tree_insert_world, globals=site_tree_globals, callees=LOCKIT_PROBES,
         ignore_bytes=(SITE_TREE_OUT + 5, SITE_TREE_OUT + 6, SITE_TREE_OUT + 7)),
]

SELF_TEST += [
    # ignore_bytes on byte +0xd of the finders' freed tree nodes: the wrong
    # farm finder stores every key one higher (node byte +0xc).
    finder_spec("WrongFarmKeys", 0x0059ac50),
]

SELF_TEST += [
    # ignore_bytes on byte +0xd of every fake-heap block (the site lists'
    # new nodes): the wrong FillAreaSiteLists rates a Roman player's coal
    # mine places twice (more nodes, other keys).
    Spec("WrongFillSiteLists", 0x00556630, "thiscall", [one_of(WORLD_BASE + AREA_SOURCE), one_of(0, 1, 2)],
         eax_mask=0, presets=fill_site_lists_world, globals=site_list_globals, callees=LOCKIT_PROBES,
         ignore_bytes=SITE_LIST_NODE_PADDING),
]

SELF_TEST += [
    # ignore_bytes on AddArea's new node (tree comparator / allocator bytes,
    # four type -> tree pointers): the wrong AddArea leaves the iron mine's
    # countdown at 0 without the basic economy.
    Spec("WrongAddArea", 0x00555260, "thiscall", [add_area_area, add_area_player], eax_mask=0,
         presets=add_area_world, globals=add_area_globals,
         callees={**FORCE_CALLEES, **LOCKIT_PROBES, 0x00556630: (2, "thiscall", "probe", None, add_area_record_fill)},
         ignore_bytes=ADD_AREA_IGNORED, max_cases=100),
]
