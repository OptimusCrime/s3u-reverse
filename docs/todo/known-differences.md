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
- **UI range leftovers (session 57, wip/ui-controls):** not done in
  0x400000..0x4c0000: CControl::ShowTooltip 0x418550 and the
  FillBackground/Draw members that call GDI (CTextView/CListBox
  FillBackground, CProgressBar::Draw, CStaticSurface::Draw) need fake
  DC/font API handlers in emu.py; the App_sub helpers around
  0x411490..0x4114d0 call the old iostream lock (MSVCIRT 0x61d1b5); the
  small CCampaignDialog / lobby getters (0x413b80, 0x413ba0, 0x413bf0)
  want their classes named first.

## Postponed by decision
- **Network and lobby code** (CNetThread, Lobby::*, LobbyClient::*,
  DirectPlay, CLM* messages): skipped since 2026-11-09 (single player
  against computer players is easier to verify; online play may not matter
  today). Revisit when the single-player game is far along.
