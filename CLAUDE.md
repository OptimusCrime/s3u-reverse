# Settlers 3 Reverse

## Goal

Reverse engineer and reimplement **The Settlers 3 â€“ Ultimate Collection**
(Blue Byte, 1998; GOG release).

1. **Phase 1 (current):** reverse the original code and build a reimplementation
   that runs on Windows and behaves exactly like the original game.
2. **Phase 2 (later):** port the reimplementation to macOS.

Correctness is the top priority. Every reimplemented piece must be checked
against the original game's behavior, not just "looks right".

## Session routine

- **At the start of a session:** read `LOGBOOK.md`. Its "Current focus"
  section says what to do next. If analysis is needed, open `S3.EXE` in REA
  (`open_binary`, then any query; Ghidra analysis takes about 2â€“3 minutes).
  Then re-apply our names with `set_addresses_names`, using the output of
  `python tools/inventory.py names`.
- **While working:** permanent facts and procedures go in this file. Data
  goes in `re/`. Progress goes in `LOGBOOK.md`. Things that should be done
  at some point but not now go in `TODO.md` (the backlog); when picking the
  next piece of work, check it too, and remove items when they are done.
- **Before stopping, or after finishing a piece of work:** update
  `LOGBOOK.md` (current focus, next steps, open questions, and a dated
  session-log entry), then commit.
- **After every finished batch of work** (e.g. a group of replaced
  functions that passes its tests): commit **and push** (`git push origin
  main`) right away, one commit per batch, so the history on GitHub shows
  each step. The user may restart with a fresh context
  at any time, so anything only in the conversation will be lost.

## Ground rules

- The original game install lives in `Settlers3Ultimate/` and is gitignored.
  Never commit original game files (executables, DLLs, GFX, sounds, maps, music).
- The reimplementation loads game data from the user's own installation at runtime.
- Do not commit raw decompiler output. Commit hand-written, cleaned-up
  reimplementations with meaningful names.
- Line endings are normalized to LF via `.gitattributes` (repo is used on
  both Windows and macOS).

## Tools

- **REA MCP server** (`mcp__rea__*` tools) plus the `reverse-engineer-anything`
  skill: static analysis (Ghidra 12.1.4 backend), decompilation, xrefs,
  annotations, and runtime tools (Frida, GDB, rizin).
- The `rea` CLI is not on PATH; the MCP server runs without it.

## Facts about the original binaries

- **Main executables:** `S3.EXE` and `S3_multi.EXE` (3,842,048 bytes each). They
  are the same build and differ in only 7 bytes, both GOG patches:
  - At file offset `0xFE72`, `S3_multi.EXE` has `xor dl, cl` inside a
    byte-decoding loop (keystream multiplier `0xFED42531`). `S3.EXE` replaces it
    with `nop nop`, so that decoding step is disabled.
  - At file offset `0x295A1D`, `S3_multi.EXE` reads the registry value
    `SerialNumber`. `S3.EXE` reads `SingleNumber` instead, a renamed value that
    doesn't exist, so it skips the serial number lookup.
  - Most likely interpretation: `S3_multi.EXE` keeps the original serial/CD-key
    handling that multiplayer needs, and `S3.EXE` has it neutralized for
    single player. Treat `S3.EXE` as the reference binary.
- **Compiler:** MSVC 6.0 linker, PE timestamp `0x38E4B1B0` (2000-03-31). 32-bit x86.
- **Fixed load address:** relocations are stripped and there's no ASLR, so it
  always loads at `0x400000` and Ghidra addresses equal runtime addresses.
- **Known addresses:** entry point (CRT startup) at `0x621bc6`; `WinMain` at
  `0x45a120`.
- **Language:** C++ (586 RTTI class names, e.g. `Win32::CApplication`,
  `DirectX::CGameApp`, `CScreenElement`). Ships with `MSVCRT.DLL` and `MSVCIRT.DLL`.
- **Sections:** `.text` ~2.4 MB of code, `.rdata` ~0.25 MB, `.data` ~1.4 MB.
  Image base `0x400000`.
- **Size:** Ghidra's default auto-analysis finds 20,897 functions and 3,476
  defined strings. The function count includes C runtime/library code and
  small compiler-generated helpers, so the game's own code is smaller.
- **Imports:** DirectDraw (`ddraw`), DirectInput, DirectSound, DirectPlay
  (multiplayer; `directplay.cmd` enables it), WinSock, WinINet, plus
  `ProtectCD.dll` (copy protection reference) and `_INMM.dll` (GOG CD audio
  emulation).
- **Other binaries:** `S3Editor/S3Editor.exe` (map editor), `SETUPS3.EXE`
  (config tool), `FileConvert.exe`, `BGLWINR.DLL`, `NEWTOOLR.DLL`, `PATCHW32.DLL`.
- **Game data:** `GFX/`, `SND/`, `MUSIC/`, `Map/`, `Save/`, `DATA.DAT`.

## Approach (decided)

**Incremental replacement inside the running original game**, the approach
used by OpenRCT2 and re3:

1. A 32-bit DLL of our own (`s3re.dll`) is loaded into the original `S3.EXE`
   by `s3re_launcher.exe` using Microsoft Detours
   (`DetourCreateProcessWithDllEx`). Nothing is added to the game folder.
   A proxy `ddraw.dll` won't work: `S3.EXE` loads DirectX dynamically, and its
   static imports are only system DLLs plus GOG's `_INMM.dll`.
2. Original functions are redirected (hooked) one at a time to our C++
   reimplementations. The game stays playable at every step.
3. Every replacement is verified:
   - **Function level:** differential tests call the original and our
     version with the same inputs and compare outputs and side effects.
   - **System level:** if the simulation is deterministic (likely, since
     lockstep multiplayer depends on it; still to be confirmed), record inputs,
     replay them in the original and in the hooked build, and compare
     game-state hashes every tick.
4. Work bottom-up: leaf functions and file formats first, then subsystems.
5. When nothing original remains, build a standalone executable, then replace
   DirectX with a cross-platform layer (e.g. SDL) for macOS.

Never mark a function as done without verification evidence.

**Unverified reimplementations (decided 2026-11-09).** Coverage may run
ahead of verification: a reimplementation without evidence yet is
registered with `S3RE_HOOK_UNVERIFIED` and gets inventory status
`reimplemented`, hook `unverified` (`inventory.py check` enforces the
pairs verified/replaced and reimplemented/unverified). Such hooks are only
installed with `s3re_launcher --unverified`, so default runs and whole-game
checks run verified code only. "Unverified" means "no evidence yet", not
"skip easy evidence": write the emulator spec whenever it is quick.
`frontier.py` counts reimplemented functions as done (`--verified-only`
doesn't). When the evidence exists, switch to `S3RE_HOOK` and `verified`.

Rejected alternatives: a from-scratch rewrite can't prove equivalence. Matching
decompilation (byte-identical recompile with MSVC 6) is too slow for this much
C++ code.

## Build environment

- **Language:** C++. Our classes mirror the original's where practical.
- **Toolchain:** Visual Studio 2026 Community (v18) at
  `C:\Program Files\Microsoft Visual Studio\18\Community`, with MSVC toolsets
  14.16 through 14.51 and the x86 compiler installed.
- CMake and Ninja come bundled with VS (not on PATH):
  - `...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - `...\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- The hook DLL must be built **32-bit (x86)** to load into `S3.EXE`.
- **Build:** `cmake --preset x86`, then `cmake --build --preset debug`.
  Output goes to `build/x86/bin/Debug/` (`s3re_launcher.exe`, `s3re.dll`).
  Run the launcher to start the game. It writes `s3re.log` next to the DLL.
- **Code layout:**
  - `src/launcher/`: checks the SHA-256 of `S3.EXE` and starts it with the DLL.
  - `src/s3re/`: the DLL. Hooks are registered with `S3RE_HOOK` (replacement)
    or `S3RE_PASSTHROUGH` (observe and call the original), see `hooks.h`.
    Game code goes under `src/s3re/game/`, one file per class or subsystem.
    MSVC 6.0 library code that game data depends on (e.g. `std::string`
    layout) goes under `src/s3re/msvc6/`.
  - `src/s3re/test/`: the differential test harness (`difftest.h`).
  - `re/functions.csv`: the function inventory (source of truth for names,
    status and verification). `re/ghidra_functions.tsv`: Ghidra's function
    list (the baseline). `tools/inventory.py`: check, stats and names.
- **Coding style:** `docs/coding_style.md` (the user's decisions,
  2026-11-22): readable idiomatic C++ with identical behaviour, named
  values instead of magic numbers (shared enums in
  `src/s3re/game/enums.h`, evidence in `re/enums.md`; typed access to the
  category fields through `game/typed_fields.h`), decimal except
  addresses/offsets/masks, comments tracing back to original addresses.

## Runtime structure

How the game runs (verified by reading the code; names are in
`re/functions.csv`):

```
WinMain (0x45a120)
  refuses to start after 2037
  CConsoleThread app (on WinMain's stack)   application class; derives from
                                            DirectX::CGameAppDraw -> ... -> Win32::CThread
  Win32::CThread::Execute (0x60e2d0)        vtable slots: 4 OnStart, 5 Main, 6 OnExit
    CConsoleThread::OnStart (0x40b3f0)      initialization, starts the other threads
    CConsoleThread::Main    (0x404860)      UI/message loop (not the simulation)

Game threads (CLogicThread, CDrawThread, CNetThread, CNetBounceThread)
  DirectX::CGameThread::Main (0x5b63d0)     loop: quit/stop flags, slot 10 OnEvent,
                                            otherwise slot 9 Step
  CLogicThread::Step (0x53db30)             one simulation step:
    world = this->[+0x3a0]
    world->UpdateEnvironment (0x4e7810)     map/landscape, timed events
    world->UpdateEconomy     (0x4e05e0)     economy, 18 time slices (was "UpdatePhase2")
    world->UpdateSettlers    (0x4e9670)     settler jobs (provisional)
    0x550f80 on 20 objects at this+0x350    per player? (unknown)
    game-end check 0x511c50, tick bookkeeping
```

- **The world object** (`CGameData`, the original class name from RTTI;
  0x153A128 bytes, vtable at +0) holds the whole simulation
  state. Known offsets used by our code are in `src/s3re/game/world.h`.
  The map is stored in rows of `0x300` tiles of 24 bytes; the map size is at
  `+0x2d2d0`.
- **How a game's world is built** (`CLogicThread::OnStart` 0x53c9e0):
  a map file goes through the map loader `0x4fd540`; a map with a
  random-map chunk (chunk 5, "Erstelle Zufallswelt") through the random map
  generator `CGameData::GenerateRandomMap` (`0x5166d0`, seeds the RNG itself); a
  savegame through `0x508fa0`. So the recorded-game and savegame checks
  never call GenerateRandomMap (no random-map start tooling yet); both other paths end
  in `CGameData::BuildRegionsAndAreas` (`0x51a580`).
- **Field knowledge lives in the world-layout files:** `re/world_layout.md`
  (memory map of the whole object, with confidence levels), `re/structs.md`
  (field tables per record: tile, area, pile, settler, building, player...)
  and `src/s3re/game/layout.h` (the C++ structs, in the build).
  `python tools/world_fields.py` lists every world displacement in
  `build/s3.asm` grouped by region and record field (lower bounds; check
  single fields by hand). New findings about fields go into
  `re/structs.md` / `re/world_layout.md`, not only into code comments.
- **Typed access, no raw offsets (decided 2026-11-09).** Our code reads and
  writes the world through the packed structs of `src/s3re/game/layout.h`
  (`world->tiles[x][y].owner`, `world->settlers[n].job`,
  `world->players[p].race`; record types in namespace `layout`, every
  offset `static_assert`ed). Tiles start at `0x67444` (settled,
  `re/structs.md`). Code that carries a tile as a 32-bit byte offset and
  steps it past the map (as the original does) uses `TileOffset` /
  `TileAtOffset`. Other objects (UI classes, CLogicThread, Watcom vectors,
  ...) get small packed structs with `static_assert`s next to the code or
  in a shared header. New field knowledge goes into the structs (and
  `re/structs.md`), never into new offset constants in `.cpp` files;
  unknown fields are named `unkXXXX`. The only raw helper is `RawAt` in
  `game/raw.h`, for genuinely unstructured bytes, with a comment. Tests use
  `WorldPair::Both` / `SameField` / `OffsetOf` or `offsetof`.
  `python tools/typed_access_lint.py` (part of `inventory.py check`) flags
  old `At<`/`WorldField`/`world_offset::` uses, byte casts of the world
  and world-range offset constants; a justified exception carries `raw-ok`
  in a comment.
  **Globals (2026-11-22):** every S3.EXE data address (0x63a000..0x7ed000)
  is declared once, typed, in `game/globals.h` (variables, e.g.
  `inline constexpr GameGlobal<std::uint32_t> g_gameTick{0x7dfd48};`),
  `game/tables.h` (constant tables), `game/texts.h` (strings, `GameText`,
  passed to `GameDebugLog`) or `game/vtables.h` (vtables, vbtables, throw
  info), and recorded in `re/globals.csv`. `GameGlobal`
  (`game/global_access.h`) is constant-initialized (works in the emulator)
  and used like a variable (`g_gameTick & 0x3f`, `g_settlerTypeClass[t]`,
  `g_config->unk189`, `&g_playerTallies`); write `.get()` for variadic or
  deducing calls (it is not copyable, so a mistake fails to compile). The
  lint rejects data-address literals anywhere else; code addresses
  (function calls, hooks) stay allowed.
- **The simulation's random number generator** is `CGameData::Random`
  (`0x4feb70`), with its state at world `+0x2d2dc`. It's reimplemented and
  verified. Because the state lives in the world, the simulation is probably
  deterministic. It's seeded by `CGameData::SeedRandom` (`0x4feb40`) at
  game start, with a different seed each game.
- **The simulation is deterministic** given the map, the RNG seed and (so
  far, no) player input, verified by unattended runs.
- **Launcher options for runtime checks:** `--trace-determinism` writes a
  per-tick trace (compare two with `tools/compare_traces.py`); `--seed N`
  forces the RNG seed; `--exit-after-ticks N` ends the game at tick N;
  `--timeout S` kills it after S seconds; `--dump-tick N` dumps the hashed
  world memory at tick N. Game options go after `--`, e.g.
  `-- -map Map\MULTI\384-2-Lakes.map` starts that map without menus.
  `--unverified` also installs the unverified reimplementations.
  `s3re.log` logs every seed and the step count.
- **Tick pacing and `--fast`:** `CLogicThread::OnStart` starts a periodic
  64 ms `Win32::CTimer` (CLogicThread+0x3bc; 15.6 ticks/s), and
  `CLogicThread::Step` waits on its wait set (+0x3dc,
  `CWin32Waitable::WaitForXYZ` `0x463100`, 500 ms) and runs a tick only
  when the timer was the signalled object; other objects wake it without a
  tick (hence ~240 steps/s). `CNetBounceThread` answers step messages at
  once, so it doesn't pace. `--fast` (`src/s3re/tooling/fast.cpp`) makes
  only that wait in Step poll and report the timer: every tick is original
  code, only the idle steps between ticks go. About 450 ticks/s on the
  384 map (1,000 ticks: ~65 s -> ~2 s of game time, ~8 s per run with
  loading). The game's own fast-forward (`g_stepCountdown` += 882) is not
  used: it skips work in the simulation. Shown identical (2026-11-10): the
  original with `--fast` twice; normal against `--fast` on
  scripted-seed127.bin (3,000 ticks) and "slot 1.sav" (2,000 ticks; this
  found the UI-only field +0x2d2c8, now skipped by trace v4); the original
  against ours with `--fast`. check_replay.py, check_save.py and
  check_determinism.py and check_random_map.py use `--fast` by default
  (`--no-fast` to opt out).
  Recording with `--synthetic-input` stays at normal speed (that input
  follows the wall clock).
- **Cached reference traces of the original:** check_replay.py and
  check_save.py `--against-original` reuse the original's trace from
  `build/trace_cache/` (`tools/trace_cache.py`: key = S3.EXE and input
  hashes, launcher arguments, trace format version and original-mode
  tooling version; a longer cached run serves a shorter one). Flags:
  `--no-cache`, `--refresh-cache`, `--verify-cache` (rerun the original and
  compare with the cache). **Version rules:** change the trace format or
  what it hashes -> bump "s3re determinism trace vN" in
  `src/s3re/trace/determinism.cpp` (old reference traces, e.g. a
  recording's `.tsv`, must then be regenerated); change any code that runs
  in `--original` mode (passthrough and `S3RE_HOOK_ALWAYS` hooks, trace,
  replay, seed, load-save, fast) -> bump `S3RE_ORIGINAL_TOOLING_VERSION` in
  `src/s3re/tooling/version.h`.
- **First-call ticks:** at exit s3re.log also has "first-call <name>
  <tick>" for each called replacement; `python tools/first_calls.py
  s3re.log` (and the checks) summarize how many were first called after
  ticks 100/500/1,000/2,000.
- **Game lock (decided 2026-11-09):** agents may run in parallel (git
  worktrees). Before any run that opens the game window (check_replay.py,
  check_determinism.py, plain launcher runs; not `--test`), create the
  directory `Settlers3Ultimate/s3re-game.lock` atomically (mkdir; if it
  exists, wait in the queue below), write `owner.txt` (who, when) into it, and
  make sure no `S3.EXE` runs. Remove it when the run ends, also on
  failure. A lock older than 2 hours with no `S3.EXE` running is stale.
  An `S3.EXE` whose parent is `s3re_launcher --test` (in-process tests,
  no window) doesn't count as running, so test runs never block game
  checks (`game_lock.running_games` checks the parent's command line;
  an `S3.EXE` without a parent counts). **Fair queue (2026-11-10):**
  waiters write a ticket `<ns time>-<pid>-<owner>.ticket` into
  `Settlers3Ultimate/s3re-game.queue/` and only the oldest live ticket
  may mkdir the lock, so runs get the game in arrival order (checked
  every 3 s; tickets and locks of dead pids are removed; the ticket is
  removed on exit, errors and Ctrl+C). `held(owner, max_wait=None)`,
  `acquire`/`release`; `python tools/game_lock.py status` shows the
  holder, its age and the queue. A lock made by hand (mkdir, no
  `owner.txt`) is still respected. `python tools/test_game_lock.py`
  tests it without a game.
  `tools/game_lock.py` implements it (`with game_lock.held(owner):`);
  `check_replay.py`, `check_determinism.py`, `check_save.py` and
  `check_random_map.py` take it automatically; `python tools/game_lock.py status|release`. Manual
  launcher runs take it by hand. Never write to `Settlers3Ultimate/Save/`
  (`--load-save` and `check_save.py` only read and copy the savegames).
  In a worktree, make `Settlers3Ultimate` a junction to the main
  checkout's (`New-Item -ItemType Junction`; gitignored). The launcher
  starts S3.EXE from the junction's real path: Windows registers the
  `DWM8And16BitMitigation` compatibility layer S3.EXE needs by path, and
  without it DirectDraw fails (`DDERR_UNSUPPORTED` in
  `CDirectDraw::DDSetDisplayMode`, an "Error!" box).
  **Removing a worktree: delete the junction first** (`cmd /c rmdir
  <worktree>\Settlers3Ultimate`, which removes only the link), then
  `git worktree remove`. `git worktree remove --force` follows the
  junction and deletes the main checkout's game files (happened
  2026-11-10; restored from `Settlers 3 Ultimate.zip` in the GOG install
  folder `C:\Program Files (x86)\GOG Galaxy\Games\Settlers 3 Ultimate`,
  whose contents equal the original copy: same S3.EXE hash, same saves).
- **Starting from a savegame:** `--load-save FILE` (a path, or a name in
  `Save\`, e.g. `"slot 1.sav"`) starts the game directly from that
  savegame, with the game's own loader: the launcher passes a copy
  (`s3re-loaded.sav` next to the DLL) as `-map`, and at the menu thread's
  first update (`Menus::CDrawThread::Update` `0x493880`, state 0x14) the DLL
  calls `GameSettings::LoadSaveGame` (`0x432b00`, as the load dialog does)
  and then does what the dialog's result 10 does (`0x494e03`).
  `CLogicThread::OnStart` then runs the savegame loader `0x508fa0`; the RNG
  state comes from the file (`--seed` has no effect). With a loaded save,
  `--exit-after-ticks N` counts from the save's start tick. Not combinable
  with `-map` or the command recorder. Code: `src/s3re/game/savegame_start.cpp`.
  `python tools/check_save.py "slot 1.sav" --ticks N [--against-original]`
  runs it unattended (twice original, or original vs ours), compares the
  traces, checks that no savegame in `Save\` changed, and writes the call
  counts to `save-check/<save>-calls.tsv`. Results: `re/savegame_checks.md`.
- **Starting a random-map game:** `--random-map SIZE[,SYMMETRY[,FIELD0C]]`
  (sizes 128..768 in steps of 64; the dialog offers 384 and up; symmetry
  bits 0..3) starts a game on a map from the game's random map generator
  (`0x5166d0`) without menus. The launcher passes `-map` (default
  `Map\MULTI\384-2-Lakes.map`), whose player setup is used; a pass-through
  hook on `GameSettings::NewGame` (`0x431ab0`), only at the `-map` call site
  in `Menus::CDrawThread::Update` (return address `0x493a56`), replaces the
  map file with a memory-only `Dirk::CMapFile` holding chunk 5 (size, a
  second dialog value, symmetry, seed) and chunk 1 (random-map kind, start
  positions), built exactly like the map dialog does (`0x481e90`).
  `CLogicThread::OnStart` then calls GenerateRandomMap with that chunk. `--seed N` also
  becomes the map seed (GenerateRandomMap seeds the RNG through `SeedRandom`); without
  `--seed` the launcher picks a random one. `--goods low|medium|high` (start
  goods, NewGame arg 9), `--computers N` (N computer opponents, one party
  each; rewrites NewGame's party vectors) and `--race R` (your race, via a
  pass-through on `GamePlayerSetup::Set` 0x49f120) go with it; there is no
  computer-player difficulty in the game (`re/game_setup.md`, which also
  documents NewGame's arguments). Combinable with the command recorder
  (recording version 3 stores the setup; `--replay-commands` restarts the
  same game), not with `--load-save`. Code:
  `src/s3re/tooling/random_map_start.cpp`. `python
  tools/check_random_map.py --maps "384,0;448,3" --ticks N
  [--against-original] [--map FILE]` runs it unattended (twice original, or
  original vs ours) and records call counts as source `random:SIZE,SYM seed N`.
- **Recording a whole game played by the user:** `python
  tools/play_game.py` (defaults: 768 random map, 2 computer players, high
  goods, random seed, `--original`, normal speed, trace on; takes the game
  lock for the session) writes `recordings/NAME.s3rec` (+ `.s3rec.tsv`
  trace and `.log`, both gitignored). User guide: `docs/recording_a_game.md`.
  Commands are flushed per command; quitting or closing the window ends the
  recording. F12 (the game's fast-forward, `g_stepCountdown` += 882) is a
  UI event that a recording can't contain; the recorder logs a warning.
  Single-player saving is not a command (ProcessEvents) and doesn't affect
  the recording.
- **Leaving out single replacements:** `--skip-hooks NAME[,NAME...]` (hook
  names as in the "hooked NAME" lines of s3re.log) installs everything
  except those, so the original runs there: to find which replacement makes
  a whole game diverge, or to count our callees of an original function
  (calls from our own replacements bypass the counting thunk; e.g.
  `check_random_map.py --against-original --skip-hooks CGameData_GenerateRandomMap`
  counts ExpandTerrain and MarkTerrainEdge).
- **Unattended determinism check:** `python tools/check_determinism.py
  [--ticks N]`. It opens the game window, so the user's screen is busy
  while it runs (with `--fast` about 10 s per 1,000-tick run, mostly
  loading; about 1 minute per 1,000 ticks with `--no-fast`).
- **Recording and replaying commands:** `--record-commands FILE` records
  every command; `--replay-commands FILE` replays a recording instead of the
  live commands. Recordings (format in `src/common/recording_format.h`)
  store the game seed, the map and the random-race setting (version 3+ also
  the `--random-map` setup). **Recordings are committed, as text** (user
  decision 2026-11-22): format version 4 (`s3re-recording 4`, one line per
  command: step, then the message bytes as hex; `#` comments), extension
  `.s3rec`, in the top-level `recordings/` (list and provenance in
  `recordings/README.md`; traces `.tsv` there are gitignored). The DLL
  writes v4 and flushes every line; all readers still read the binary
  versions 1-3 (`tools/command_log.py convert IN OUT` turns them into v4;
  `python tools/test_command_log.py` tests the formats). Tools take a path
  or a name in `recordings/` (`--recording scripted-seed127`). Whole-game
  sources are `replay:<name without extension>`. The launcher
  starts the replay with that seed and map (an explicit `--seed`/`-map`
  must agree) and the DLL stops the game (exit code 3) if the running game
  doesn't match. Old version 1 recordings need `--seed` and `-map`, or
  `tools/command_log.py upgrade`. `python tools/check_replay.py`
  checks record/replay unattended (replay identical, and a recording with one
  command removed must diverge); add `--synthetic-input SEED` to have
  `tools/synthetic_input.py` play the recorded game with random mouse input
  (SendInput; it only acts while `S3.EXE` is the foreground window).
  `tools/command_log.py list FILE` shows a recording. `--log-commands` logs
  every command to `s3re.log`.
- **UI state lives inside the world object.** Camera position, selected
  menu, overlay marks on map tiles, a scratch buffer shared by the UI and
  the simulation, and a few more change with mouse input and are not
  repeated by a replay. The determinism trace leaves them out of its hashes;
  each excluded field and the code evidence for it is listed in
  `src/s3re/trace/determinism.cpp` (keep `tools/diff_world_dumps.py` in
  sync). To find new ones: `--dump-tick N[,N...]` in both runs, then
  `python tools/diff_world_dumps.py A.bin B.bin`, then check the writers in
  the code before excluding anything.
- **A replay only makes sense in the game it was recorded in.** With
  another seed the recorded commands run in a different game, which can
  even crash the original (e.g. buildings of the wrong race). Recordings
  now carry their seed and map (see above).
  `recordings/scripted-seed127.s3rec` was recorded with seed 1234 (127 was
  the synthetic-input seed); it was upgraded to version 2 and converted to
  version 4.
- **Diagnostics in `s3re.log`:** the game's own `DebugLog` messages (688
  call sites, silent in the release build; at most 5 per call site, with
  callers; calls from our replacements show as `s3re.dll+RVA`), and for access violations in game code the registers, memory
  and callers (`src/s3re/crash_log.cpp`). The game also appends its own
  crash reports to `Settlers3Ultimate/errorlog.txt`.
- **`--exit-after-ticks` shuts the game down normally** (WM_CLOSE to its
  windows, killed after 15 s if that hangs), so threads run their OnExit.
- **The full disassembly** (`dumpbin /disasm:nobytes /out:build\s3.asm
  Settlers3Ultimate\S3.EXE`, run from PowerShell, since Git Bash mangles
  `/options`) is greppable for field offsets, e.g. `2D2DCh` to find all RNG
  state accesses. `python tools/asm_tools.py` queries it: `disasm ADDR`
  (whole functions), `leaves` (small call-free functions not yet verified:
  the next candidates), `eax-use ADDR` (do callers use the return value?),
  `xrefs REGEX` (which functions read/write a field), `callers ADDR`.
- **Picking the next functions:** `python tools/frontier.py` lists READY
  functions (not verified, every direct callee verified/CRT/DebugLog),
  ranked by how many others they unblock, then NEAR (one callee missing)
  and BLOCKERS. Default range `0x4c0000..0x560000` (simulation and logic
  thread); `--range 401000 61e4ef` for all game code. It hides the ~2,000
  static-initializer stubs (`mov ecx, global; call ctor; ret`) and lists
  identified library functions (stl/mfc/watcom/compiler) separately;
  `--library-done` counts those as done (they are not verified). Batches
  pick from READY, preferring simulation functions with callers.
- **Trivial functions count as done, proven by their bytes** (user
  decision 2026-11-09). Some bodies are too short for a Detours hook and
  have nothing to reimplement. `python tools/trivial.py list|apply|check`
  finds them mechanically: the whole body (start to next start, then only
  `nop`/`int 3`) is `ret [N]`; or one of `xor eax,eax` / `mov eax,IMM` /
  `or eax,-1` / `xor al,al` / `mov al,IMM` then `ret [N]`; or a forwarder
  `jmp T`, `add|sub ecx,IMM; jmp T`, or the vtordisp thunk
  `sub ecx,[ecx-K]; [add|sub ecx,IMM;] jmp T`, with T a known function
  start. Nothing else (getters, setters, initializer stubs). `apply` sets
  such rows to `identified` (never `verified`; verified/hooked rows are
  left alone) with a note starting "Trivial, proven by its bytes (...)",
  which quotes the instructions. `frontier.py` counts them as done, a
  forwarder only once its target is done (`--no-trivial` turns this off).
  Run `trivial.py check` after regenerating the function list.
- **Allocator bytes of MSVC 6.0 strings** (decided 2026-11-09, batch
  27). Byte +0 of a `std::string` holds its empty allocator object; it is
  never read. The compiler fills it from wherever the allocator argument
  happens to live. (a) A stack slot it shares with an argument (the low
  byte of `mostDerived`, of a pointer argument, of a count `n`): that is
  reproducible, so replicate it exactly (0x40a580, 0x408cc0, 0x41c3e0,
  0x611a20). (b) An uninitialized local of the original's own frame: not
  reproducible. If that string is freed before the function returns, use 0.
  If it survives (e.g. a returned string, 0x612e80), the byte is a known,
  behaviour-irrelevant difference: say so at the function and in its
  inventory row, leave the byte out of the in-process comparison, and
  exclude exactly that byte with `Spec.ignore_bytes` plus a self-test
  pairing.
- **Detours and tiny functions:** a 4-byte body such as `mov eax,[ecx+0Ch];
  ret` can be hooked when nop/int 3 padding follows it (Detours uses the
  padding; 0x601270).
- **Global variables** are recorded in `re/globals.csv`, e.g. the tick
  counter `g_gameTick` at `0x7dfd48`.
- **`DebugLog` (`0x428720`)** is a printf-style call that is just `ret` in the
  release build. Its format strings (German) are useful clues about what
  calling code does.
- **Functions Ghidra missed** can be disassembled by address range with
  `dumpbin /disasm /range:<start>,<end> S3.EXE` (VS `dumpbin.exe` in
  `VC\Tools\MSVC\14.50.35717\bin\Hostx64\x86`). The function end is the next
  start in `re/ghidra_functions.tsv` / `re/extra_functions.tsv`.
- **Very large decompilations** (e.g. `UpdateSettlers`, 340k characters) are
  too big for the context window. Analyze the saved tool output with a
  script (callees, strings, field offsets) instead of reading it whole.

## Commands and lockstep

How player commands reach the simulation (verified by recording and
replaying, see `src/s3re/replay/command_log.h`):

- The logic thread talks to a network thread through two `MessageQueue`s
  (ring buffers of dwords; `Push` `0x4309c0`, `Pop` `0x430980`): it pushes
  on `CLogicThread+0x3c4` and reads replies from `CLogicThread+0x3c8`.
  Messages start with a type byte; byte 2 is the size in bytes.
- Commands (player input, computer players, sync checks) are pushed as type
  `0x21` messages, byte 1 = command id. Each ExecuteStep,
  `CLogicThread::ExecuteCommands` (`0x535990`) pushes a `0x20` step message.
- In single player the network thread is `CNetBounceThread`
  (`0x46f6f0`): it sends the commands back in order (as type 1), batched
  with the reply to the next step message, plus a type 2 message that allows
  the logic to run up to step+5. `ExecuteCommands` executes the batch.
  Multiplayer uses `CNetThread` (commands become 0x12 "announce" packets
  with an ExecuteStep, `CNetThread::OnNetAnnounce` `0x46cbd0`).
- So the order of 0x21 and 0x20 messages on the logic->net queue fully
  determines which commands run at which step. Our recorder keys each
  command by the number of 0x20 messages before it.
- Bytes 9..11 of 12-byte command messages are often uninitialized stack
  memory (they differ between runs); the simulation doesn't depend on them
  as far as tested.
- The game's own `-record`/`-play` (`Siedler3.evt`, class `CEventFile`)
  record the same commands, but `-play` adds the file's commands to the live
  ones instead of replacing them, so computer-player commands run twice and
  the replay diverges. Don't use it for verification.

## Function inventory

- `re/ghidra_functions.tsv` lists the 8,863 function starts Ghidra finds.
  It also keeps the 11,817 `Unwind@`/`Catch@` exception-cleanup snippets,
  with kind `funclet`, which are never counted. Regenerate it with
  `tools/import_ghidra_functions.py` from an REA evidence bundle
  (instructions in the script). Together with `re/extra_functions.tsv`, that
  makes 9,837 known functions.
- `re/functions.csv` holds one row per function we have identified. The
  columns are documented in `tools/inventory.py`. Names use `Class::Method`.
- Ghidra names made through REA are lost when the session closes. At the
  start of a session, re-apply them from `python tools/inventory.py names`
  with `set_addresses_names`.
- `python tools/inventory.py check` must pass before committing. It checks
  every hook in `src/` against its inventory row, that the tests named in
  `verified_by` exist, and that `re/whole_game_calls.csv` matches the
  inventory (after a rename: `python tools/whole_game.py fix-names`).
- **Whole-game evidence** is kept in `re/whole_game_calls.csv`: one row per
  function and whole-game check source (`replay:<recording>`,
  `save:<savegame>`, `random:<size>,<symmetry> seed <n>`) with the largest call count, ticks, commit and date
  (columns in `tools/whole_game.py`). `tools/record_call_counts.py` (after
  check_replay), `tools/check_save.py --against-original` and
  `tools/check_random_map.py --against-original` (when identical) write it. A verified function is then "seen in a whole game"
  (some count > 0), "tests only" (counted 0 in every check) or "no
  whole-game check yet" (no row). A 0 can hide real runs: calls from our
  own replacements bypass the counting thunk (e.g. PlanSiteLevelling inside
  our CheckBuildingSite), and `S3RE_HOOK_ALWAYS` hooks aren't counted;
  `python tools/whole_game.py summary` lists the tests-only functions and
  flags those reachable from seen replacements in our code. The progress
  page and `inventory.py stats` show this split.
- The statically linked C runtime sits at the end of `.text`, roughly from
  `0x61e4ef` onwards (exact boundary unknown). Game code comes before it.
  Known runtime functions include `operator delete` at `0x61e4ef`,
  `_purecall` at `0x61f061`, and `operator new` at `0x61efb9`.

## Original source files

- `S3.EXE` has **no debug symbols** (no debug directory, symbols stripped,
  no `.pdb`/linker `.map`). But it contains 57 unreferenced `__FILE__`
  paths like `E:\Siedler3\Volker\LogicHandleNet.cpp`. The folders are
  the programmers' first names (full names from the game's credits in
  `DATA.DAT`): `Dirk\` (Dirk Ringe: app shell, UI, network, lobby),
  `Volker\` (Volker Wertich: game logic), `Rainer\` (Rainer Foetzki:
  computer players), plus `Credits\`, `DirectX\`, `Win32\`.
- Each `.obj`'s data and code are laid out in the same order, so the
  strings after a path, and the functions using them, give each named
  file a code range. `python tools/source_files.py scan` writes
  `re/source_files.csv`; `which ADDR` names the file of an address;
  `strings [FILTER]` shows a file's strings. Method, limits, link order
  and translations of the German names and messages: `re/source_files.md`.
- Only files with that assert macro left a path. Most of the simulation
  (`0x4c85a0`–`0x509980`, between DrawBuilding.cpp and GameDataSave.cpp)
  is in unnamed files. The German strings are Latin-1.
- Our source tree is not reorganized after this yet; that is planned
  separately (see `LOGBOOK.md`).

## Class structure (RTTI)

- `python tools/rtti.py scan` reads the MSVC RTTI straight from `S3.EXE` and
  writes three files:
  - `re/classes.json`: classes, bases, vtables, virtual functions, and the
    functions that store each vtable
  - `re/classes.md`: the class hierarchy, for reading
  - `re/extra_functions.tsv`: function starts that the RTTI proves exist but
    Ghidra missed (kind `rtti`). Starts found otherwise are added by hand as
    kind `manual` with their evidence (e.g. 0x582540, a static-initializer
    table entry); a rescan keeps them.
  `python tools/rtti.py apply` adds `classified` rows to `re/functions.csv`
  and never changes existing rows.
- **586 classes:** game 285, Watcom container library 248 (`WC*`), MFC 34,
  C runtime and old iostreams 16, STL 3. The Watcom classes suggest the code
  base started on the Watcom compiler.
- **Game classes** are mostly framework, UI, network and AI: `Win32::*` and
  `DirectX::*` (the app and window framework, `CSiedlerMainWindow`),
  `CScreenElement` and its UI controls, `Menus::*`, `Lobby::*` and
  `LobbyClient::*` (online play), and AI agents (`BLACK_BOARD_AGENT`,
  `T_ECONOMY_AGENT` and others). The core simulation (settlers, buildings,
  map) shows no polymorphic classes, so it's probably plain structs and
  functions that need other ways to map.
- **Ghidra missed 973 functions** that are only reachable through vtables
  (listed in `re/extra_functions.tsv`). Ghidra can't decompile them through
  REA, because REA can't create functions. Disassemble them with
  `inspect_with_objdump` instead.
- **Naming in the inventory:**
  - `Class::vfNN`: virtual function in slot NN; `_subXX` if it sits in the
    vtable of a base subobject at offset XX
  - `_thunk`: a compiler-generated this-adjusting thunk
  - `Class::scalar_deleting_destructor` / `Class::~Class`: deleting destructor
    and the destructor it calls
  - `NoOp_retN`: an empty virtual function shared by many classes through
    linker folding
  When a function's purpose becomes clear, rename it and set it to
  `identified`.
- **Names are ours and can be wrong.** `S3.EXE` has no symbols: only class
  names (RTTI) and source file names come from the binary; every function
  name was given by us from what we understood at the time, and earlier
  analysis can be wrong (e.g. `EvaluateBuildingFootprint` turned out to be
  `PlanSiteLevelling`; `CGameData::GenerateRandomMap` is the random map generator).
  **Whenever a name turns out to be misleading or wrong, rename it** (also
  for verified functions, and for structs, fields and globals): update
  `re/functions.csv` (keep the old name in the notes: "Was X"), the hook and
  code names in `src/` (test names too), `python tools/whole_game.py
  fix-names`, `re/structs.md` / `re/world_layout.md` / `re/globals.csv`
  where relevant, then `python tools/inventory.py check`. If a rename is
  large or collides with work in progress, add it to `TODO.md` instead of
  skipping it. Prefer the original's real names where the binary gives
  them (RTTI class names such as `CGameData`, German log strings).
- Constructors aren't named yet. `classes.json` lists the functions that
  store each vtable (constructors, destructors, or code that inlined them).

## Workflow: reimplementing a function

1. Analyze the function with REA: decompile, read the assembly, check
   callers and callees. Add or update its row in `re/functions.csv`
   (`identified`).
2. Write the replacement with `S3RE_HOOK`, matching the calling convention.
   Put a `// Purpose:` comment right above it: in plain language, what the
   game uses the function for (not the argument details, which go in the
   file's header comment); say "probably" where that is a guess.
   Member functions are `__thiscall`; write them as `__fastcall` with a dummy
   EDX parameter (see `hooks.h`). Check whether callers use EAX after the
   call before declaring a function `void`. A replacement that also provides
   tooling (like `--seed`) uses `S3RE_HOOK_ALWAYS` and calls the original
   itself in original mode. Follow `docs/coding_style.md`: enums from
   `game/enums.h` for jobs, types, goods and races (via
   `game/typed_fields.h`), named constants, decimal literals, helpers named
   after what each block of the original does, comments with addresses.
3. Write differential tests with `S3RE_TEST` in the same file. Run the
   original (through the `Original_*` trampoline) and our version on identical
   random inputs and starting state, then compare return values and all
   memory touched (including guard bytes past buffers). Where the comparison
   is non-trivial, also add a test that a deliberately wrong implementation
   is caught.
4. Add a spec to `tools/emutest/specs.py` and run `python
   tools/emutest/run.py [filter] --require-coverage` (no game needed; needs
   `pip install -r tools/requirements.txt`). It runs the original bytes
   from S3.EXE and our function from s3re.dll in an x86 emulator (Unicorn)
   on identical arguments and a lazily-random world, compares EAX, every
   byte written to the world/S3.EXE, bytes popped and callee-saved
   registers, and reports basic-block coverage of the original. The gate is
   0 mismatches and every block covered; add presets to a spec when random
   data doesn't reach a branch (`globals` sets S3.EXE globals, which
   otherwise keep their image values; `edx` sets EDX for __fastcall
   helpers; `cover_until`/`exempt_blocks` handle code past a false function
   start and blocks that are dead or only reachable by a C++ exception, with
   the reason in a comment). The game's operator new/delete are a bump
   allocator in the world area (calls compared), std::_Xlen/_Xran end the
   run as a throw (compared), FS points at a fake TIB so SEH frames work.
   Virtual calls: point an object's vtable at `emu.FAKE_VTABLE` with a
   preset and declare the slots in `Spec.virtuals` (calls recorded and
   compared); imported Windows functions go to API probes
   (`emu.API_HANDLERS`; add a handler for a new one). Convention
   `fastcall` passes args[0]/args[1] in ECX/EDX. `Spec.virtual_effects`
   lets a probed virtual call change memory (e.g. a child that shrinks its
   parent's list), `Spec.new_fails` makes operator new return null in some
   cases, `Spec.max_instructions` raises the per-call limit (20M) for
   inputs our debug build needs more for, `Spec.case_scale` runs N times
   `--cases` (UpdateSettlers: 12, one job family per case), and `Spec.ignore_bytes` leaves out output bytes that provably hold
   each function's own stack leftovers (padding of a copied stack
   temporary; say why at the spec and add a self-test pairing that differs
   next to them). timeGetTime and Sleep are probed too (read timeGetTime
   through S3.EXE's import 0x655358, which in-process tests can point at a
   fake clock). COM methods: a `Spec.virtuals` entry `(count, pointers,
   {"com": True, "returns": f})` records the call without ECX (stdcall,
   interface as the first argument) and lets `f(h)` pick EAX (e.g. a set of
   HRESULTs); a pointer argument's recorded bytes can be `((offset, size),
   ...)` for structures with uninitialized fields. `Spec.callees` probes
   calls of S3.EXE functions that are not reimplemented and can't run in
   the emulator (exception constructors, the CRT's free): recorded with
   their arguments, returning `this` or a probe value. `_CxxThrowException`
   0x61f027 ends the run like _Xlen/_Xran and is compared with the thrown
   object pointer and throw info. In-process, a game C++ exception can be
   caught with SEH (`__try`/`__except`, code 0xE06D7363; ExceptionInformation
   [1] is the address of the thrown value), see directx_surface.cpp.
   `run.py --self-test` checks that the
   harness catches deliberately wrong pairings; `--all-uncovered` lists
   every uncovered block (to write the dead-block reasons at a spec). Our replacements must not
   rely on DLL startup code (the emulator doesn't run it); call the game's
   operator new/delete/memmove (0x61efb9/0x61e4ef/0x61e860) where the
   original does.
   - **Parallel runs:** specs run in `--jobs N` worker processes
     (default, 2026-11-23: the free logical cores minus 2, measured over
     0.5 s, so cores busy with other agents' work count as taken; capped
     so that 80 % of the free memory holds 400 MB per worker plus the
     extra of each selected spec with `Spec.memory_mb`, e.g.
     CGameData_UpdateSoldier 1,600; or `S3RE_EMU_JOBS`; workers run at
     below-normal priority; `--jobs 1` runs in one process; the run
     prints the count and why). The user's machine became unresponsive
     with 18 workers while other agents were busy: when several agents
     work in parallel, only one of them runs a full suite at a time (per
     batch use `--changed`). Every spec starts from a
     fresh emulator state (`Emulator.reset`: images as loaded, stack
     zeroed, no world bytes, Unicorn's translated-code cache flushed),
     and every call starts with an empty x87 stack and fixed flags, so
     results don't depend on the order or the worker count, and the
     output is printed in specs.py order. Python writes over S3.EXE code
     (a stub in `Spec.globals`) go through `Emulator.write_image`, which
     drops the stale translated blocks: Unicorn doesn't notice them (a
     WalkSettler FindPath stub ran the real FindPath after
     Pathing_FindPath in the same worker, 2026-11-22).
     The slowest specs start first (times in the gitignored
     `build/emutest_times.json`), and specs that took longer than about
     total/jobs/2 run as several parts (case ranges; the inputs of the
     skipped cases are still drawn, so the merged result equals a whole
     run; `--self-test` checks this for two specs, with each part run
     after another spec). Full suite on 18 workers: ~15 min wall time (the serial run
     takes over an hour); a worker peaks at ~380 MB. `--memory` prints
     workers, peak memory and wall time. `--bin-dir DIR` tests the DLL in another
     build directory (e.g. the main checkout's).
   - **Selective runs (by compiled code, 2026-11-23):** `run.py
     --changed` runs only the specs without a recorded pass for their
     current fingerprints: the compiled code of the replacement in
     s3re.dll plus everything of ours it reaches (calls, data, pointers
     in data; addresses normalized to symbol names, so moved code
     doesn't count), its specs.py entry (with the helpers it uses;
     comments don't count) and the harness (emu.py, requirements, the
     `re/*_functions.tsv` lists, the case-running functions of run.py).
     So a header change that doesn't change code (a new enum, comments,
     moved lines) selects nothing, and a changed callee selects the
     specs of everything that reaches it. Every run with the standard
     inputs (2000 cases, seed 1; any filter, quick or full) records its
     passes (0 mismatches and full coverage, or all its cases run) in
     `build/emutest_passes.json` of the **main checkout**, shared by
     all worktrees (a fresh worktree reuses them); a harness that hasn't
     passed `--self-test` yet runs the self-test too. `--dry-run`
     prints the selection. Measured: a new enum in enums.h selects 0
     specs (by headers: all); readability commit 571aab8 15 specs (by
     headers 92), fb7e93c 41 (82). The older header-based selection
     stays available: `--since REF` or `--changed --by-headers` (tracking
     logs + `build/emutest_last_full.json`, written by full runs with 0
     mismatches). Details: `tools/emutest/changed.py`.
   - **Quick mode** (the default of `--changed`; `--quick` elsewhere):
     a spec stops once every block is covered and at least 25 % of its
     cases ran (the cases that ran are exactly the first ones of a full
     run, so a quick pass is "0 mismatches + full coverage" too); no new
     case starts after `--budget` seconds (default 60): reported as
     "budget", fine with full coverage, otherwise a warning listing the
     specs and no recorded pass. Quick runs don't split specs into parts.
     `Spec.quick_cases` caps very slow specs whose main evidence is
     elsewhere (CGameData_GenerateRandomMap: 1 case of ~100 s; its
     evidence is the in-process whole-world tests and the random-map
     whole-game checks). `--full` runs every case without budget.
   - **Build (2026-11-23):** run.py tests the **Release** build by
     default (`cmake --build --preset release` first; its full suite
     matched Debug's, 0 mismatches, several times faster); `--build Debug`
     for debugging a mismatch. Passes are recorded per compiled code, so
     each build has its own.
   - **Policy:** small and medium functions get full emulator evidence;
     large ones (GenerateRandomMap, UpdateSettlers, UpdateSoldier) get
     bounded emulator runs per batch and rely on the in-process tests
     and the whole-game replays. Per batch: `run.py --changed
     --require-coverage` (quick mode; it includes the batch's new
     specs). At the end of each parallel round of agents and before
     milestones: the full suite with `--full`. Run `--self-test` when
     the harness changed (`--changed` does it by itself then).
5. Build, then run `s3re_launcher --test [filter]` (substrings, several
   separated by `|`, e.g. `"Fog|Sight"`). This runs the tests inside
   the real `S3.EXE` without opening the game window. The exit code is 0 only
   if every test passed. A crashing test is logged with the faulting
   instruction as module+offset (look the s3re.dll offset up in
   `s3re.map`, preferred base 0x10000000) and the address accessed.
6. Whole-game check (at milestones, not per batch; it's slow):
   `python tools/check_replay.py --recording <file> --against-original
   --ticks 3000` replays a recorded game with the original code
   (`s3re_launcher --original` leaves out all replacements) and with ours,
   and the traces must be identical. It only covers functions the game
   actually called: replacements run behind a counting thunk, the counts
   are logged at exit ("calls <name> <n>" in s3re.log) and check_replay.py
   lists the ones that were never called. Record the call counts with
   `python tools/record_call_counts.py --recording NAME --seed S --ticks T`
   (inventory notes and `re/whole_game_calls.csv`; savegame checks record
   themselves); never claim whole-game evidence for an uncalled
   function. Keep
   recordings worth keeping in `recordings/` (committed, with a line in
   its README; make one with `check_replay.py --synthetic-input SEED`, the
   recording is `replay-check/recording.s3rec` next to the DLL).
7. Set the row to `verified`, `hook=replaced`, `verified_by=<test names>`
   (space-separated); without evidence yet: `reimplemented`,
   `hook=unverified` and `S3RE_HOOK_UNVERIFIED` (see "Unverified
   reimplementations" above). Run `python tools/inventory.py check`.
   Then run `python tools/progress.py` to regenerate the progress data
   (`docs/progress-data.js`, read by the static page `docs/progress.html`)
   and the README picture (`docs/progress.svg`), and commit them with the
   batch.
8. Occasionally play the game normally with all hooks active, as an
   integration check.
- The user is new to Visual Studio. Drive builds from the command line, and
  explain any IDE steps the user has to take.
- The original game runs fine on the dev machine (Windows 10), so runtime
  comparison against the original is possible.
