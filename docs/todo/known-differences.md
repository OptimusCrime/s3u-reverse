# Known differences and postponed work

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Known differences to clean up
- Some replacements leak temporaries on fatal error paths because they have
  no exception-unwind frames (std::string `_Xlen`/`_Xran` paths, the throw
  helpers 0x5ffcc0 / ThrowOutOfMemory, ...). Harmless today (the game
  ends), but worth fixing for the standalone build. List: inventory notes
  containing "Known difference".

- **Win32::CRegistryKey (wip/app-r4, win32_registry.cpp):** after
  ERROR_NO_MORE_ITEMS, EnumKey copies an uninitialized FILETIME local
  (every caller passes null) and GetValueInfo goes on with an
  uninitialized type and an unwritten name buffer (callers stay below
  GetValueCount); ours use zeroed locals. The tests' fakes avoid these
  paths. GetSubKey's local key is closed while an exception unwinds (as in
  the original); if that RegCloseKey also fails, both terminate.

- **Flaky in-process test WatcomIsvSList_MatchOriginal (found in
  wip/app-r4, also fails on main's Release build about one run in three):**
  the arena comparison relocates every word that points into an arena, so
  a random fill word that happens to fall inside one side's arena (heap
  addresses differ per run) is rebased on that side only. Fix: relocate
  only the words the functions wrote as links, or fill with values outside
  both arenas.

## Libraries and the standalone build
- **C runtime functions the game code calls, and which behaviour must be
  copied exactly (session 51).** In hook mode the game's statically
  linked MSVC 6.0 CRT keeps running; the standalone build will use
  today's CRT, which differs in details. List every CRT function
  (category `crt`, ~0x61e4ef onwards) called from game code, first the
  simulation range 0x4c0000..0x560000 (`python tools/asm_tools.py
  callers ADDR`, or a script over build/s3.asm), and mark for each
  whether its exact behaviour matters: `_ftol` 0x61fe98 (truncation),
  `qsort` (order of equal elements), `sprintf` formatting, `rand`/`srand`
  (the simulation uses CGameData::Random instead?), string/memory
  functions (no difference). The result becomes a short list in `re/` of
  CRT behaviour our code must reproduce.
- **Confirm the x87 control word at runtime** (the floating-point rule,
  CLAUDE.md, assumes 53-bit precision, round to nearest): log
  `_controlfp(0, 0)` once per game thread (e.g. a pass-through on
  `CLogicThread::Step`, original-mode tooling: bump the version), in a
  normal game run. Details: docs/reimplementation_notes.md.
- **Prove the third-party library identifications** (zlib 1.1.3,
  libpng 1.0.2, PaintLib; category `thirdparty`, re/picture_code.md)
  against the published sources. Low value: only the online lobby's
  banner uses them. Do it if the lobby is ever taken up, or for the
  standalone build. Open Watcom's container sources may be read as a
  reference for the `WC*` classes, but not copied (its licence is not
  GPLv3-compatible); our layout-compatible versions stay in
  `src/s3re/watcom/`.

- **UI control constructors without SEH unwinding (session 57,
  game/ui/controls.cpp):** CControl, CButton, CImageButton, CTextButton,
  CAnimatedButton, CScreenGroup, CGroupControl and CScreen constructors
  have exception frames in the original that destroy the members built so
  far if a callee throws; ours have none (the callees don't throw in
  practice; only an out-of-memory in a string or vector would). Add
  cleanup if a whole-game check or a crash ever points there.

- **UI range left after wip/ui-gdi:** CScreenGroup::~CScreenGroup 0x416da0
  (needs WCIsvSListBase::clearAndDestroy 0x60b560 / base_next 0x60b400)
  unblocks the CGroupControl, CDialog, CVScrollBar, CProgressBar, CUpDown,
  CTextView destructors; CStaticText::SetText 0x41c810 unblocks
  CUpDown::UpdateText/SetValue/SetRange and CProgressBar::SetProgress;
  CListBox::UpdateLayout 0x41f550 (OnResized); the remaining GameConfig
  members (0x412380, 0x4125c0, 0x412830, 0x411bc0, 0x411d10 serial check,
  0x413f20); CEventFile, CCreditsDialog::OnMessage20 0x492750,
  CMenuSharedData::IsRequestSuperseded 0x4682d0, the reciprocal table
  (0x4a1eb0, the CGameData constructor: done in wip/logic-r4).

- **Framework throwers and VERIFY wrappers (session 58,
  wip/framework-errors):** ThrowWin32Error / ThrowHResult / ThrowTimeout /
  ThrowMMError / ThrowMCIError leak their message while unwinding (no
  exception frame, as ThrowOutOfMemory); CHResult::Describe of a
  non-Win32 HRESULT whose FormatMessageA fails reads an uninitialized
  pointer in the original (ours: null, text skipped). Still open in the
  framework: the CRegistryKey members, CTimer Start/Stop, CThread::Start,
  CMessageQueue::ProcessMessage and the other ThrowWin32Error callers in
  0x5b0000..0x620000 (same VERIFY shape as win32_synchronize.cpp; the
  ThrowLastError helper there fits them).

- **png_chunk_warning / png_chunk_error 0x5a9f80 / 0x5a9ea0 (wip/images-r4,
  thirdparty/png_error.cpp):** libpng 1.0.2's stack buffer is 0x50 bytes,
  but png_format_buffer writes up to 82 (four non-letter chunk name bytes
  as "[XX]" plus ": " plus 64 message bytes), overwriting the low half of
  the return address; the original png_chunk_warning then returns to a
  wrong address. Ours has room for 82 bytes. Only a corrupt PNG with a
  non-letter chunk name gets there (and only the lobby banner uses PNG).
- **Stack garbage in libpng / zlib outputs (wip/png-r6):** huft_build
  0x5b2410 copies a stack temporary into every Huffman table entry, so
  bytes 2..3 of each entry (union padding, never read) hold the original's
  stack contents; ours writes 0. png_handle_tIME 0x5ae970 passes a stack
  png_time whose padding byte png_set_tIME copies to info->mod_time[7]
  (ours: 0). png_read_finish_row 0x5afe20 leaves zstream.next_out pointing
  at a byte of its own stack frame. Tests and specs leave these bytes out.
- **png_do_read_interlace 0x5af7f0 (wip/png-r6):** the original copies a
  pixel through an 8-byte stack buffer; pixel depths above 64 bits (not
  possible from a valid IHDR) would overrun it. Ours has 32 bytes.

## Postponed by decision
- **Network and lobby code** (CNetThread, Lobby::*, LobbyClient::*,
  DirectPlay, CLM* messages): skipped since 2026-11-09 (single player
  against computer players is easier to verify; online play may not matter
  today). Revisit when the single-player game is far along.
