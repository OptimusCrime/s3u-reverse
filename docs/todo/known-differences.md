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

- **CButton::IsPressed 0x41a990 / SetChecked 0x41aa20 / CDropdown::OnFocusChanged 0x422770 unverified (session 58,
  wip/ui-gdi, ui_button_scroll.cpp):** both start with `mov al, [ecx+disp8]`;
  called through their Detours trampolines in-process, the originals
  returned a wrong AL (0x71 / 0x30 where the bytes give 1 / 0), so they
  are S3RE_HOOK_UNVERIFIED with emulator evidence only. Find out whether
  the trampoline (4-byte body + nop padding) is broken, which would also
  matter for other tiny hooks. OnFocusChanged (Release build only) gave AL
  0xd8 although its last call is `xor al, al` (0x415c90); Debug passed.
- **UI range left after wip/ui-gdi:** CScreenGroup::~CScreenGroup 0x416da0
  (needs WCIsvSListBase::clearAndDestroy 0x60b560 / base_next 0x60b400)
  unblocks the CGroupControl, CDialog, CVScrollBar, CProgressBar, CUpDown,
  CTextView destructors; CStaticText::SetText 0x41c810 unblocks
  CUpDown::UpdateText/SetValue/SetRange and CProgressBar::SetProgress;
  CListBox::UpdateLayout 0x41f550 (OnResized); the remaining GameConfig
  members (0x412380, 0x4125c0, 0x412830, 0x411bc0, 0x411d10 serial check,
  0x413f20); CEventFile, CCreditsDialog::OnMessage20 0x492750,
  CMenuSharedData::IsRequestSuperseded 0x4682d0, the reciprocal table
  0x4a1eb0 (CGameData constructor).

## Postponed by decision
- **Network and lobby code** (CNetThread, Lobby::*, LobbyClient::*,
  DirectPlay, CLM* messages): skipped since 2026-11-09 (single player
  against computer players is easier to verify; online play may not matter
  today). Revisit when the single-player game is far along.
