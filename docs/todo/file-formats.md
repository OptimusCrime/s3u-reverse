# File formats, graphics, and function-start fixes

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## File formats (re/file_formats.md)
- **Corrections from the naming pass (session 50):** `g_strengthDivisor`
  0x7acfa4 is the number of active players (`g_activePlayerCount`;
  UpdateEnvironment's 0x600 / (n + 4)); `g_playerTallies` 0x7acfd4 is +0x2c
  into a 20 x 0x44-byte player-info table at 0x7acfa8 (re-base the global
  with the real record); the world regions' original names from the save
  labels ("timer" = effects +0x36700, "warmap" = presence_a/b,
  "activesettlers" = player_lists, "diggertab" = site_cache +
  footprint_scratch, which is saved, so not scratch; "sectors", "events",
  "messages"): rename in layout.h / re/world_layout.md. Dirk/Pathing.cpp's
  range in re/source_files.csv probably wrongly includes the CMapFile
  stream 0x4a55e0..0x4a5c80.
- **Reimplementation order (the CMapFile container, LoadSaveGame, LoadMap
  and its callees are verified, sessions 51-54; re/file_formats.md):** next
  the savegame writer `CLogicThread::SaveGame` 0x509980 (1,365 lines, 131
  calls: directories, the game setup writer 0x433490, the computer players'
  save 0x551220, which sits in the AI range). Optional: the Watcom skip list
  (0x60bab0/0x60bbe0/0x60be20) and `GameSettings::PlayerSetup` 0x4351e0
  (LoadMap calls the original; Watcom vector code behind a mutex) as our
  code.
- **Shared catch-type helper:** the SEH filter that matches MSVC 6.0
  catchable types exists twice (execute_commands.cpp,
  map_file_stream.cpp); make it one helper.
- **Graphics loader (re/graphics_formats.md):** session 55 verified the
  accessors, spans, RLE helpers, all blitters, the Draw methods and Close.
  Left: `CGraphicsData::Open` 0x5f6fd0 (~3,500 instructions: file search,
  CFile/CFileMapping, header checks, the 8 tables, pixel-format masks),
  `FindDirectoryContainingFile` 0x5f6a10, `DetachFromMedium` 0x5fa7c0,
  the constructor 0x5f67c0 / destructor 0x5f68b0 / scalar deleting
  destructor 0x5f6890. They need the Win32 mapping classes first
  (`Win32::CFileMapping` ctor 0x61a3a0, UnmapView 0x61aa40, MapView
  0x61a7d0, `Win32::DetachMemory` 0x61ab60, CFile 0x6195c0..0x61a080).
  The constructor's std::string at +0x24 gets its allocator byte from an
  uninitialized local (docs/reimplementation_notes.md, case b). An
  in-process test can run Open itself (no DirectDraw needed) on two
  objects and compare. The false starts 0x5f65f2/0x5f674b
  (DrawGuiImage_tail*) and 0x5fd902 (SequenceFrame_tail) are inside
  verified functions.
- **Function-start fixes found in the naming pass (session 50):**
  missing starts (add as `manual` rows in re/extra_functions.tsv with
  evidence): 0x5a0920, 0x5a0950, 0x5a1a60, 0x5a42c0, 0x5a4610, 0x5a4970
  (re/picture_code.md); the 13 draw spans and the halve span were added
  in session 55. Also missing: the static initializers 0x5f5a20,
  0x5fda80, 0x5fdb30 after the graphics code. False starts (keep out of the counts, e.g. a
  `notfunction` kind): the 26 `ProtectCD_RunEmbeddedCheck_junk_*` rows
  inside the obfuscated 0x5b2f70, the catch-continuation points 0x5fe948,
  0x5fe9d8, 0x5feb05 inside CSoundData::Open, the `_tail` rows 0x5f6c3d,
  0x5f65f2, 0x5f674b, 0x5fd902, and 0x551637 inside DeactivateNote.
  The source-file range `Credits\cpp_exo_text__diverses.h`
  0x5a0d20..0x5b50ad really ends at the credits code (~0x5a4220).
  More from the DirectX/Win32 rounds: missing starts 0x5b7810 (the
  EnumDisplayModes callback), the six surface-copy candidates around
  0x5c3a40..0x5c40f0 (re/directx_framework.md), 0x60ec20, 0x610380,
  0x610040, 0x6101a0, 0x615bd0, 0x615bb0, 0x618a30
  (re/win32_framework.md); false starts 0x5c45e3 (after a catch of
  0x5c4520), 0x615601 (`CCDAudio::Init_tail`). Source-file ranges
  around DirectX\ are shifted by about one file (netsession.cpp's last
  rows are Ddraw.cpp, the DirectSound wrapper sits in "Dplay.cpp",
  0x5c3740..0x5c4140 in "Dsound.cpp" is CPU detection and the copy
  benchmark); ExceptionHandler.cpp 0x61b990..0x61c4b8 is not Detach.cpp;
  0x615520/0x615601 belong to cdaudio.cpp. Fix re/source_files.csv's
  boundaries (`source_files.py scan`) or add overrides.
- **Left from the verified-name corrections (session 51 did the
  renames):** the CSimpleSurface/CSurface field corrections in
  re/directx_framework.md.
- **Function starts from the UI rounds:** Missing function starts of the UI rounds (dialog handlers merged into
  their neighbours, e.g. 0x47df70, 0x47efd0, 0x4882e0, 0x48d930,
  0x48d9e0; 0x41e410, 0x421260, 0x4271e0) and false starts (0x419b5f,
  0x427cab, 0x49859c) are listed in re/ui_classes.md; the rows 0x47ecc0,
  0x482d00, 0x486ea0, 0x48d620 span more than one function. 0x42c2d0..
  0x42ca70 belong to Draw.cpp, not dialog.cpp.
