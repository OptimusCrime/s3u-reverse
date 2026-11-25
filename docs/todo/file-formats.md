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
  and its callees are verified, sessions 51-54; the savegame writer
  `CLogicThread::SaveGame` 0x509980 with `GameSettings::MapName`,
  `~CMapFile` 0x4a5b50, `SaveSetup` 0x433490, `PlayerSetup` 0x4351e0 and
  the caller `CLogicThread::SaveGameToSlot` 0x50ac40 since session 55;
  re/file_formats.md):** left on the savegame path: the CRT's `_mkdir`
  0x621556 and `time` 0x61f6a7 (SaveGame calls the originals),
  AI::SaveGlobals 0x551220 (AI range). Settings side (the GamePlayerSetup
  copy constructor, operator= and Set and the 16-bit string members
  _Copy/_Grow/_Split/assign_substr are verified, session 57):
  `GameSettings::LoadSaveGame` 0x432b00 (~730 lines). Its Watcom vector
  members (player/team resize and insertAt, CTeam copy/assign/AddPlayerId,
  GamePlayerSetup::LogSlot; ui/game_settings_vectors.cpp) are verified
  (session 57); left among its callees: 0x433e50 (380 instructions, "CFG:
  Searching for NetId %i", with 0x437950, 0x433a90, 0x437e20, 0x4a0e10,
  0x433c80, 0x49f360, 0x49f190 and Win32::CLock) and the trivial 0x413bd0.
- **Logic range leftovers (session 57):** CGameData::InitPlayerInfo
  0x4fd0d0 (321 instructions of string temporaries; needs
  GameSettings 0x4318d0, a byte +0x29 read under the lock, and the lobby
  name getter 0x5db260; InitAfterMapLoad calls the original meanwhile);
  CGameData::DumpTableToFile 0x4ff6d0 (debug dumps to d:\*.dmp through
  the CRT's fopen/fprintf; a test would need a redirected path);
  Logic_sub_4d6740 (debug text through 0x4a8bd0 / 0x4a8e00) and
  Logic_sub_4d8400 (draws with it); CLogicThread::~CLogicThread 0x53c7d0
  (needs ~CGameThread 0x5b60a0 and the Watcom list code). The row
  CLogicThread::ExecuteCommands_tail 0x53b839 is not a function (frontier
  lists it as READY; it belongs to the false starts below). Optional: the Watcom skip list (0x60bab0/0x60bbe0/0x60be20).
- **Graphics/sound loader leftovers:** the graphics loader (session 55
  round 2) and the sound loader (session 56: CSoundData ctor/dtors, Open,
  Close, Detach, GetEffect, PurgeEffects, volume; the DirectSound wrapper
  classes; re/sound_formats.md) are verified. The effect classes (CSoundEffect, CMemorySoundFile/CSoundFile, the
  dictionaries' Watcom code, CDirectSoundException, CDirectSound slot 1, the
  getters 0x5ff4d0/0x5ff4e0) followed in session 57 (wip/sound-fx). Left on
  the sound side: the lobby-side CSoundData callers 0x5fff00 / 0x600170; the
  other exception destructors of the same shape as 0x5c1830 (0x5b93c0,
  0x5b94c0, 0x5be3e0, 0x5c0010, 0x60e490, 0x612790, 0x612c80, and the lobby
  ones) could share DestroyMessageException (directx_sound_files.cpp).
  CD music (Win32::CCDAudio, session 57 did the small members): Init
  0x615520, Open, Close 0x615e20, DoPlay, the window procedure 0x615bd0
  (missing function start) need ThrowMCIError 0x6114f0 / CMCIException
  0x613fc0 / Describe 0x614250 first (re/win32_framework.md).
  The false starts 0x5f65f2/0x5f674b
  (DrawGuiImage_tail*), 0x5fd902 (SequenceFrame_tail) and 0x5f6c3d
  (FindDirectoryContainingFile_tail) sit inside verified functions.
- **Test isolation (session 55):** MapFileStream_MatchOriginal failed once
  while two worktrees ran it at the same time (shared %TEMP% file names);
  give temp files per-process names.
- **Function-start fixes found in the naming pass (session 50):**
  missing starts (add as `manual` rows in re/extra_functions.tsv with
  evidence): 0x5a0920, 0x5a0950, 0x5a1a60, 0x5a42c0, 0x5a4610, 0x5a4970
  (re/picture_code.md); the 13 draw spans and the halve span were added
  in session 55 (the static initializers 0x5f5a20, 0x5fda80, 0x5fdb30
  in session 55 round 2). False starts (keep out of the counts, e.g. a
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
