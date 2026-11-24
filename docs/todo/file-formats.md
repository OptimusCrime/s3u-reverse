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
- **Reimplementation order (the CMapFile container, LoadSaveGame and
  LoadMap's callees are verified, sessions 51-53; re/file_formats.md):**
  next `CGameData::LoadMap` 0x4fd540: split into helpers per chunk (6
  landscape, 8 buildings, 7 settlers, 9 piles, 10 victory conditions,
  0x40), the colours (GameSettings::PlayerSetup 0x4351e0 per player:
  reimplement or call the original), UnblockCastleTiles and the
  campaign-mission fix-ups; test every map under Map\ (read only) into
  worlds from `map_test::ConstructWorld`. Then the savegame writer
  `CLogicThread::SaveGame` 0x509980. Optional: the Watcom skip list
  (0x60bab0/0x60bbe0/0x60be20) as our code.
- **Whole-game evidence for the loaders** (each opens the game window):
  `check_save.py "slot N.sav" --ticks 2000 --against-original` for slots
  1..3; `check_random_map.py --maps "384,0;448,3" --ticks 1000
  --against-original` (AddPlayerTowns, PlaceTown, PrepareLoadedTerrain);
  `check_replay.py --recording scripted-seed127 --against-original
  --ticks 3000` (Lakes.map through LoadMap and AddPlayerTowns).
- **Shared catch-type helper:** the SEH filter that matches MSVC 6.0
  catchable types exists twice (execute_commands.cpp,
  map_file_stream.cpp); make it one helper.
- **Graphics (re/graphics_formats.md, session 50):** the game's graphics
  are the memory-mapped `GFX\siedler3_NN.<pixel format>.dat` files read by
  CGraphicsData (8 tables, RLE sprites, palettes by brightness); the draw
  spans 0x5f4980..0x5f5910 pick by brightness. Reimplement the loader and
  the sprite decoding first (verifiable by comparing decoded bytes).
- **Function-start fixes found in the naming pass (session 50):**
  missing starts (add as `manual` rows in re/extra_functions.tsv with
  evidence): 0x5a0920, 0x5a0950, 0x5a1a60, 0x5a42c0, 0x5a4610, 0x5a4970
  (re/picture_code.md) and 13 draw spans plus the halve span 0x5fd0c0
  (re/graphics_formats.md). False starts (keep out of the counts, e.g. a
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
