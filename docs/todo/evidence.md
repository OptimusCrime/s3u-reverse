# Evidence and testing

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Evidence and testing
- **Static-initializer stubs left over (stubs-r5, 2026-11-26).**
  `python tools/static_inits.py scan` lists them as class `other` (plus
  the ctor stubs): 28 compiler rows, ~1.5 KB. The constructor stubs
  (`mov ecx, g; jmp ctor`, 9; `push 10; push 5; mov ecx, g; call
  WCValOrderedVector_HFONT ctor 0x427d60`, `push 3; push 8; ... 0x616020`)
  would fit JumpWithThis/CallWithThis and the emulator's callee probes,
  but an in-process test can't re-run a container constructor on the
  live global safely (save/restore the object and compare what the
  constructor writes, ignoring heap addresses). Also: the eh vector
  constructor/destructor iterator calls (0x546950/0x546980, 0x58f8c0/
  0x58f8f0, 0x58f940/0x58f970: stdcall 0x61ff7f / 0x61f18a), the guarded
  destructor stubs 0x4155f0 / 0x615bb0, the constant stores 0x58f860
  (`= 500`) / 0x5a0930 (`= 0`), 0x60b9e0 (`g = rand()`), and the
  iostream initializers of the CRT (0x61ca0a.., 0x61d61c..).
- **C runtime depends-on list (2026-11-27, re/crt_dependencies.md).**
  Status `platform` exists (300 crt rows; tools/crt_dependencies.py).
  Left: name the 206 candidates' unnamed CRT_sub_/IOS_sub_ rows, re-run
  `crt_dependencies.py apply` when a candidate turns out OS-only (file
  search, thread start, ...), and verify the rest (_ftol, new/delete,
  memmove, string and sprintf family, rand, 64-bit helpers).
- **DirectDraw renderer, what is left (wip/ddraw-render, 2026-11-26).**
  The fake DirectDraw (`test/fake_ddraw.*`) and the emulator setup
  (`ddw_*` / `dgd_*` in specs/framework.py: COM probes, import slots of
  SetRect / SendMessageA / CoCreateInstance patched to probes) cover
  Ddraw.cpp and most of CGameDrawer. Done in wip/logic-r4: SetDisplayMode
  0x42ce90, ChangeDisplayMode 0x4c7490, ApplyScreenLayout 0x4c6d40, the
  CLandscapeSurface constructor 0x54bd30, PlayAmbientSounds 0x4c7730.
  Not done yet:
  CGameDrawer's constructor / destructor / Shutdown / EnumDevicesCallback
  and the display-mode callback 0x5b7810 (not in the function lists yet)
  need the WCValOrderedVector<CDirectDrawDevice/CDisplayMode> code
  (0x5b8080 resize, 0x5b8330, 0x5b8560, 0x5b69e0, 0x5b7ec0/0x5b7f40) and
  ~CDirectDrawDevice 0x5b7670 first; Render_sub_54cbd0 (the landscape
  drawing, ~2000 lines, locks the surface with CSurface::Lock) is large.
  The panel texts are done (wip/text-r6: Dirk::CTextSurface, the CDrawer
  text helpers 0x4d65e0..0x4d78d0, InitPanelFonts, the message texts,
  BlitVisibleArea, WideToNarrowString); left in the frame drawer's tree:
  (CDrawer::DrawPanel 0x4b4d00 is done, wip/panel-r7; slot 1.sav ran it 529
  times identically; a recording with its menus open would cover more),
  Render_sub_4c85a0 (the
  settler drawer, 38 KB, 11,250 instructions; its dumpbin listing desyncs
  on a jump table, so frontier.py never lists it), Render_sub_4b26c0 (needs
  both and 0x54cbd0), CDrawer::SetMouseCursor 0x42cb60 (its callees are
  done now; it has a catch(...) around DrawSprite that unlocks the mouse
  surface and rethrows, which needs an exception test), CDrawThread::Draw /
  FetchDrawData (0x42c710 / 0x42c780). CTextSurface's constructor,
  destructor and its font deleter 0x4a7d30 need the Watcom skip-list code
  (0x60b9f0, 0x60bd80, the iterator), and DrawText's wide sibling caller
  chain is done. WideToNarrowString's rejected-character path (wcstombs
  -1: an unterminated buffer, errno through TlsGetValue) has no evidence.
- **ExecuteCommands whole-game evidence (session 47).** Reimplemented and
  verified (in-process + emulator); the recordings contain ids 0x01, 0x03,
  0x04, 0x05, 0x09, 0x0b, 0x0c, 0x0f, 0x10, 0x11, 0xfb. Run the round-end
  replays/saves against the original and record the call count; the
  multiplayer/UI paths (0x06 text, 0xf4, 0xf7..0xff, messages 3..0x22)
  have tests only. The user's full game (fullgame-20261124-0055-seed10779,
  session 49) adds ids 0x06, 0x07, 0x08, 0x0e, 0x14, 0x15, 0x18, 0xf1 and
  0xf5 (identical to the original over 138,402 ticks); ExecuteCommands is
  one hook, so per-id counts would need a temporary counter.
- **UpdateSettlers handlers with tests only (session 45).** The
  whole-game checks (two replays, three saves, a random map) never reach
  these UpdateSettlers jobs; the user's full-game recording
  (fullgame-20261124-0055-seed10779, session 49) is identical to the
  original and recorded; which of these jobs it reached is unknown until
  a per-job hit count exists. A per-job hit count needs a temporary counter
  in update_settlers.cpp's dispatch. Jobs (hex):
  building 0x38, 0x6a; drinks 0xe2..0x127 (rice farmer, winegrower,
  distiller/brewer/powder maker, charcoal burner); food 0x6c..0x73,
  0xce..0xd3, 0x131, 0x132; metal 0x32..0x34, 0x3a..0x41, 0x52..0x54,
  0x56, 0x5c..0x5f, 0xd6..0xe1, 0x6b, 0x7d..0x82, 0xaf, 0x128..0x139,
  0x186..0x18f; special 0x134, 0x136, 0x13a..0x14f, 0x150..0x160,
  0x164..0x171, 0x17c..0x185, 0x190..0x194; units 0xae and the walking
  jobs 0xe5, 0xe8, 0xed, 0xf1, 0xfe, 0x102, 0x133, 0x135, 0x140, 0x141,
  0x147..0x14a, 0x165, 0x168; the 20 no-op jobs.
- **Full serial emulator run as a cross-check of `--jobs`:** the parallel
  runs (3 schedules: specs.py order, longest first, slow specs split into
  parts) gave identical output; the `--jobs 1` run was stopped after its
  first 70 specs (all identical). Run `python tools/emutest/run.py
  --require-coverage --jobs 1` once overnight and compare it (without the
  `[...s]` times) with a parallel run's output. (Session 45 found and
  fixed one schedule dependence: stale translated code after a spec's
  stub over FindPath; `--self-test` now checks split runs.)
- **186 verified functions have only test evidence** (never called
  in a whole-game check; see the progress page's tests-only list). Done
  so far: random maps (session 40) and an 8-player recording
  (fun8-seed2024-input31.bin). Still missing: longer games (10,000+ ticks,
  needs faster runs), a human-race variety, the user's late-game savegame
  (promised for later). 41 of them may run uncounted (called directly
  by our own replacements); `--skip-hooks CALLER` counts them (the
  original caller then calls our callee through the thunk).
- **Integration play:** the user hasn't played a normal game with all
  hooks active yet (CLAUDE.md workflow step 8).

- **Longer and more varied fast runs:** with `--fast` a 10,000-tick
  recording takes about a minute; record one (synthetic input records at
  normal speed, so a long one takes real time once) and check whether
  more replacements get called late (first-call ticks, `tools/first_calls.py`).
- **Run normal vs `--fast` on the other savegames** ("slot 2/3.sav") once;
  only "slot 1.sav" and the recording are shown identical so far. Watch
  `+0x2d2d8` (per-step counter, see LOGBOOK open questions).
- **Trace cache refinements:** cache the original's runs for
  check_determinism.py and check_random_map.py too; prune old entries.
- **Normal vs `--fast` on a random map:** check_random_map.py uses
  `--fast` by default, but normal vs fast has only been compared on file
  maps and a savegame.

- **Live game vs its replay: header and rest hashes differ (session 49).**
  `compare_traces.py` of the user's live trace
  (`recordings/fullgame-20261124-0055-seed10779.s3rec.tsv`) against the
  original's replay of it: RNG state and map hash identical on all
  138,402 ticks, but the header hash differs from tick 192 (right after
  the player's first commands) and the rest hash from tick 4,992. Most
  likely world fields written by the live UI (mouse, menus) that the
  replay doesn't repeat and the trace doesn't skip yet; the replays of
  the original and ours are identical, so the verification evidence
  stands. To find them, the live game needs world dumps: let
  `play_game.py` pass `--dump-tick` at a few ticks (e.g. 200,5000;
  ~22 MB each), replay with the same `--dump-tick`, then
  `tools/diff_world_dumps.py`, check the writers and add skips
  (trace version bump).

## Spells: whole-game evidence
The dispatcher CGameData::ExecuteSpell 0x5480B0 and all spell handlers
of Volker\GodlyEvents.cpp are verified (spells.cpp, spells_land.cpp,
spell_units.cpp, spell_goods.cpp; packages S1/S2 of session 46). The
user's full game (fullgame-20261124-0055-seed10779, session 49) reaches
ExecuteSpell, HasManaForSpell, PayForSpell, SpawnSpellEffects,
SpellRevealSpot and SpellTransformLand; the other spell handlers still
have tests only. Note: SpellConvertGoods (0x549BD0) hangs the original game when
cast within ~8 tiles of the map edge (kept).
- **Spec parts: duplicate top-level names (found session 56).** `null_string_count`
  is defined in common.py and again (differently) in ui.py; ui.py's wins for
  every part at call time (msvc6.py's string specs included). Also
  duplicated: PLAYER_LIST_LENGTHS, PLAYER_LISTS, ring_offsets,
  PLAYER_RECORDS, CAMPAIGN_MISSION, GAME_TICK, SETTLER_HIGH_WATER (ai.py vs
  common/logic/settlers). Check whether the values differ and rename; this
  is the LOGBOOK's "tooling guard" item.
- **Missing function start 0x41f6b0 (found by wip/menus-r5):** a CListBox
  member right after CListBox::UpdateLayout (ret 8: stores +0x410, the
  listener +0xb8's slot 0, Invalidate) is not in re/*_functions.tsv;
  CListBox_UpdateLayout's spec uses cover_until=0x41f6a1. Add it as a
  `manual` row of re/extra_functions.tsv (changes every spec's harness
  fingerprint, so together with other list changes).

- libpng reading (wip/libs-r7): png_read_info / row / image / end 0x5a5180 /
  0x5a5440 / 0x5a5890 / 0x5a58f0 and png_read_start_row / finish_row
  0x5b01c0 / 0x5afe20 are reimplemented with full emulator evidence but no
  in-process test. A whole-decode test (generated PNGs, the six originals
  reached through ScopedJumps to their trampolines) hung in the original
  run's row phase on its first case; find out why (a jump over a Detours
  hook into the trampoline, or the test's file) and finish it.
