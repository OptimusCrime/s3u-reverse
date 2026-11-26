# Running the game: launcher options and whole-game checks

Moved out of `CLAUDE.md` on 2026-11-24. How to run the original game with
our DLL unattended, and the whole-game checks built on it. Before any run
that opens the game window, take the game lock (`docs/tools/game_lock.md`).

## Launcher options

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
  `CWin32WaitableBag::WaitFor` `0x463100`, 500 ms) and runs a tick only
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
  `src/s3re/tooling/trace/determinism.cpp` (old reference traces, e.g. a
  recording's `.tsv`, must then be regenerated); change any code that runs
  in `--original` mode (passthrough and `S3RE_HOOK_ALWAYS` hooks, trace,
  replay, seed, load-save, fast) -> bump `S3RE_ORIGINAL_TOOLING_VERSION` in
  `src/s3re/tooling/version.h`.
- **First-call ticks:** at exit s3re.log also has "first-call <name>
  <tick>" for each called replacement; `python tools/first_calls.py
  s3re.log` (and the checks) summarize how many were first called after
  ticks 100/500/1,000/2,000.
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
  with `-map` or the command recorder. Code: `src/s3re/tooling/savegame_start.cpp`.
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
- **Leaving out single replacements:** `--skip-hooks NAME[,NAME...]` (hook
  names as in the "hooked NAME" lines of s3re.log) installs everything
  except those, so the original runs there: to find which replacement makes
  a whole game diverge, or to count our callees of an original function
  (calls from our own replacements bypass the counting thunk; e.g.
  `check_random_map.py --against-original --skip-hooks CGameData_GenerateRandomMap`
  counts ExpandTerrain and MarkTerrainEdge). A name ending in `*` leaves
  out every hook starting with it (session 57; e.g. `"StaticInit_*,StaticDtor_*"`
  for the static-initializer stubs; the list is limited to 4 KB).
- **Diagnostics in `s3re.log`:** the game's own `DebugLog` messages (688
  call sites, silent in the release build; at most 5 per call site, with
  callers; calls from our replacements show as `s3re.dll+RVA`), and for access violations in game code the registers, memory
  and callers (`src/s3re/crash_log.cpp`). The game also appends its own
  crash reports to `Settlers3Ultimate/errorlog.txt`.
- **`--exit-after-ticks` shuts the game down normally** (WM_CLOSE to its
  windows, killed after 15 s if that hangs), so threads run their OnExit.
  When the watchdog kills it, s3re.log has "hang:" lines with EIP and the
  return addresses on every thread's stack (module+offset; s3re.dll
  offsets via `s3re.map`), and the "calls" lines are missing (they are
  written at DLL detach), so the checks record no call counts. Session 57:
  a Debug-build stack mismatch in CDrawer::PlayAmbientSounds made the
  game lose activation and StopThreads wait forever.

## Determinism, recording and replay

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
  `src/s3re/tooling/trace/determinism.cpp` (keep `tools/diff_world_dumps.py` in
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
- **Recording a whole game played by the user:** `python
  tools/play_game.py` (defaults: 768 random map, 2 computer players, high
  goods, random seed, `--original`, normal speed, trace on; takes the game
  lock for the session; `--map NAME` plays a map of the game instead, e.g.
  `--map M704-2-Longrun`: the launcher's `--race`/`--goods`/`--computers`
  also work with `-- -map FILE`, stored as recording setup flags 2,
  `kSetupMapOptions`) writes `recordings/NAME.s3rec` (+ `.s3rec.tsv`
  trace and `.log`, both gitignored). User guide: `docs/recording_a_game.md`.
  Commands are flushed per command; quitting or closing the window ends the
  recording. F12 (the game's fast-forward, `g_stepCountdown` += 882) is a
  UI event that a recording can't contain; the recorder logs a warning.
  Single-player saving is not a command (ProcessEvents) and doesn't affect
  the recording.

## Whole-game check (workflow step 6)

Whole-game check (at milestones, not per batch; it's slow):
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

## Whole-game evidence

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

- **Don't run `--test` from the same build folder during a whole-game
  check** (session 54): `check_random_map.py` reads `s3re.log` next to the
  DLL, and a concurrent `s3re_launcher --test` overwrites it, so the check
  reports "the random map was not set up".
- **Keep the game window focused during whole-game checks** (session 55):
  if it loses focus (a click elsewhere, a locked screen), the game throws
  `CAppNotActiveException` ("Application is not active") and the check
  reports "run failed (exit code 4294967295)". Rerun that check.
