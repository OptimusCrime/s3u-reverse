# Settlers 3 Reverse

## Goal

Reverse engineer and reimplement **The Settlers 3 - Ultimate Collection**
(Blue Byte, 1998; GOG release).

1. **Phase 1 (current):** reverse the original code and build a reimplementation
   that runs on Windows and behaves exactly like the original game.
2. **Phase 2 (later):** port the reimplementation to macOS.

Correctness is the top priority. Every reimplemented piece must be checked
against the original game's behavior, not just "looks right".

## Where things are

This file holds rules, decisions and pointers; it is loaded into every
session and every subagent, so **keep it short** (aim: under ~300 lines).
Details go into the documents below; read the one you need, when you need
it.

| Document | Contents |
|---|---|
| `LOGBOOK.md` | current focus, open questions, last three sessions (older: `docs/history/`) |
| `TODO.md` -> `docs/todo/*.md` | the backlog, one file per area |
| `docs/coding_style.md` | the style guide, typed access to the world and globals |
| `docs/reimplementation_notes.md` | decided special cases (MSVC 6 string allocator bytes, Detours on tiny functions) |
| `docs/tools/emulator.md` | the emulator differential tests (`tools/emutest`): specs, presets, parallel/selective/quick runs, policy |
| `docs/tools/runtime_checks.md` | launcher options (`--fast`, `--seed`, `--load-save`, `--random-map`, `--skip-hooks`, ...), trace cache and its version rules, recording/replay, whole-game checks and evidence |
| `docs/tools/game_lock.md` | the game lock, its queue, and worktrees (junction rules) |
| `docs/tools/analysis.md` | disassembly and `asm_tools.py`, `frontier.py`, `trivial.py`, the function inventory, original source files, RTTI and naming conventions |
| `docs/recording_a_game.md` | the user's guide to recording a game |
| `docs/source_layout_plan.md` | history and plan of the source tree layout |
| `re/*.md`, `re/*.csv` | findings: world layout and structs, enums, file formats, subsystems, `re/commands_lockstep.md`, `re/functions.csv` (the inventory) |

## Session routine

- **At the start of a session:** read `LOGBOOK.md`. Its "Current focus"
  section says what to do next. If analysis is needed, open `S3.EXE` in REA
  (`open_binary`, then any query; Ghidra analysis takes about 2-3 minutes).
  Then re-apply our names with `set_addresses_names`, using the output of
  `python tools/inventory.py names`.
- **While working:** rules and decisions go in this file, procedures and
  tool details in `docs/` (`docs/tools/`), findings and data in `re/`,
  progress in `LOGBOOK.md`. Things that should be done at some point but
  not now go in the backlog (`docs/todo/<area>.md`, index `TODO.md`);
  when picking the next piece of work, check it too, and remove items
  when they are done.
- **Before stopping, or after finishing a piece of work:** update
  `LOGBOOK.md` (current focus, next steps, open questions, and a dated
  session-log entry; move entries older than the last three sessions to
  `docs/history/logbook-YYYY-MM.md`), then commit.
- **After every finished batch of work** (e.g. a group of replaced
  functions that passes its tests): commit **and push** (`git push origin
  main`) right away, one commit per batch, so the history on GitHub shows
  each step. The user may restart with a fresh context
  at any time, so anything only in the conversation will be lost.
- **Parallel agents** work in git worktrees made by hand (`git worktree
  add`), never removed with `--force`; read `docs/tools/game_lock.md`
  first (game lock, the `Settlers3Ultimate` junction). Only one agent at
  a time runs the full emulator suite.

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
    Game code goes under `src/s3re/game/` (decided 2026-11-23, plan and
    history in `docs/source_layout_plan.md`; the move was made by
    `tools/move_sources.py`). The shared headers stay in `game/` itself
    (`world.h`, `layout.h`, `globals.h`, `global_access.h`, `tables.h`,
    `texts.h`, `vtables.h`, `raw.h`, `typed_fields.h`, `replacements.h`,
    `debug_log.h`, and `enums.h`, an umbrella over the category headers
    in `game/enums/`); everything else is in subsystem folders:
    - simulation (portable): `world/` (CGameData basics: RNG, players,
      statistics, counters, id allocators, tables, notifications), `map/`
      (tiles, directions, areas and regions, territory, flood fills, fog
      of war, minimap), `mapgen/` (random map generator), `environment/`
      (UpdateEnvironment, map effects), `economy/` (UpdateEconomy, goods
      piles), `buildings/`, `settlers/` (UpdateSettlers, settler records,
      walking, soldiers) with `settlers/jobs/` (the job handlers),
      `pathing/` (Dirk/Pathing.cpp), `spells/` (Volker/GodlyEvents.cpp),
      `logic/` (CLogicThread, game end, victory conditions, UI state in
      the world) with `logic/commands/` (ExecuteCommands), `ai/`
      (computer players), `fileformats/` (the map and savegame
      container `Dirk::CMapFile`, re/file_formats.md);
    - platform side: `ui/` (CScreenElement and controls, menu layouts),
      `graphics/` (graphics data, DirectDraw surface), `net/` (CNetThread,
      lobby buffer), `framework/` (WinMain, Win32/DirectX framework
      classes, exceptions, string members).
    Rules: a new file goes into the subsystem folder of its main function
    (a new folder needs a line here and a group in
    `src/s3re/CMakeLists.txt`); `.cpp` base names stay unique across
    `src/s3re` (`tools/emutest/changed.py` keys objects by base name);
    includes are relative to `src/s3re` (`"game/pathing/pathing.h"`),
    never `../`; no catch-all files. Which original source file a
    function sat in is data (`re/source_files.csv`), not a folder.
    MSVC 6.0 library code that game data depends on (e.g. `std::string`
    layout) goes under `src/s3re/msvc6/`, Watcom container code under
    `src/s3re/watcom/`.
  - `src/s3re/test/`: the differential test harness (`difftest.h`).
  - `src/s3re/tooling/`: original-mode tooling (`--fast`, `--random-map`,
    `--load-save`), with `tooling/trace/` (determinism trace) and
    `tooling/replay/` (command recorder and replay).
  - `re/functions.csv`: the function inventory (source of truth for names,
    status and verification). `re/ghidra_functions.tsv`: Ghidra's function
    list (the baseline). `tools/inventory.py`: check, stats and names.
- **Coding style:** `docs/coding_style.md` (the user's decisions,
  2026-11-22): readable idiomatic C++ with identical behaviour, named
  values instead of magic numbers (shared enums in
  `src/s3re/game/enums/`, one header per category: `races.h`,
  `settler_types.h`, `buildings.h`, `goods.h`, `piles.h`, `spells.h`,
  `job_ids.h`, `commands.h`, `terrain.h`, `objects.h`, `resources.h`,
  `directions.h`, `value.h`;
  include the ones you use, `game/enums.h` includes all; evidence in
  `re/enums.md`; typed access to the
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
  DirectX::CGameThread::Main (0x5b63d0)     loop: quit/stop flags, slot 10
                                            OnActivate(bool) on an activation
                                            request, otherwise slot 9 Step
  CLogicThread::Step (0x53db30)             one simulation step:
    world = this->[+0x3a0]
    world->UpdateEnvironment (0x4e7810)     map/landscape, timed events
    world->UpdateEconomy     (0x4e05e0)     economy, 18 time slices (was "UpdatePhase2")
    world->UpdateSettlers    (0x4e9670)     settler jobs (provisional)
    CComputerPlayer::Step (0x550f80)        on the 20 pointers at this+0x350
                                            (null = human or empty slot)
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
  never call GenerateRandomMap (`--random-map` does); both other paths end
  in `CGameData::BuildRegionsAndAreas` (`0x51a580`).
- **Field knowledge lives in the world-layout files:** `re/world_layout.md`
  (memory map of the whole object, with confidence levels), `re/structs.md`
  (field tables per record: tile, area, pile, settler, building, player...)
  and `src/s3re/game/layout.h` (the C++ structs, in the build).
  `python tools/world_fields.py` lists every world displacement in
  `build/s3.asm` grouped by region and record field (lower bounds; check
  single fields by hand). New findings about fields go into
  `re/structs.md` / `re/world_layout.md`, not only into code comments.
- **Typed access, no raw offsets (decided 2026-11-09; full rule in
  `docs/coding_style.md`).** Our code reads and writes the world through
  the packed structs of `src/s3re/game/layout.h` (`world->tiles[x][y].owner`,
  `world->settlers[n].job`; every offset `static_assert`ed); other objects
  get small packed structs. New field knowledge goes into the structs and
  `re/structs.md`, never into offset constants in `.cpp` files; the only
  raw helper is `RawAt` (`game/raw.h`). Every S3.EXE data address is
  declared once, typed, in `game/globals.h` / `tables.h` / `texts.h` /
  `vtables.h` (`GameGlobal`) and recorded in `re/globals.csv`.
  `tools/typed_access_lint.py` (part of `inventory.py check`) enforces it;
  a justified exception carries `raw-ok` in a comment.
- **The simulation's random number generator** is `CGameData::Random`
  (`0x4feb70`), with its state at world `+0x2d2dc`. It's reimplemented and
  verified. Because the state lives in the world, the simulation is probably
  deterministic. It's seeded by `CGameData::SeedRandom` (`0x4feb40`) at
  game start, with a different seed each game.
- **The simulation is deterministic** given the map, the RNG seed and (so
  far, no) player input, verified by unattended runs.
- **Commands and lockstep:** player and computer commands reach the
  simulation as messages through the logic/net message queues; in single
  player `CNetBounceThread` echoes them back in order. Details,
  including why the game's own `-record`/`-play` can't be used for
  verification: `re/commands_lockstep.md`.
- **Running the game for checks:** the launcher can run the game
  unattended (`--fast`, `--seed`, `--exit-after-ticks`, `--load-save`,
  `--random-map`, `--record-commands`/`--replay-commands`,
  `--trace-determinism`, `--skip-hooks`, `--unverified`, `--original`),
  and `tools/check_replay.py`, `check_save.py`, `check_random_map.py`,
  `check_determinism.py` and `play_game.py` build on it:
  `docs/tools/runtime_checks.md`. **Take the game lock before any run
  that opens the game window** (`docs/tools/game_lock.md`; the check
  scripts take it themselves). Never write to `Settlers3Ultimate/Save/`.
  Rules that matter when you change tooling: code that runs in
  `--original` mode -> bump `S3RE_ORIGINAL_TOOLING_VERSION`
  (`src/s3re/tooling/version.h`); a change to the trace format or what
  it hashes -> bump "s3re determinism trace vN"
  (`src/s3re/tooling/trace/determinism.cpp`).
- **Recordings are committed, as text** (format v4, `recordings/*.s3rec`,
  list in `recordings/README.md`); a replay only makes sense with the
  seed and map it was recorded with (recordings carry them).

## Function inventory and names

- `re/functions.csv` is the source of truth for names, status and
  verification (columns in `tools/inventory.py`; `source_file` is
  generated by `tools/source_files.py apply`). `python tools/inventory.py
  check` must pass before committing. Details, Ghidra's function lists,
  RTTI, the original source files and the naming conventions
  (`Class::vfNN`, `_thunk`, `NoOp_retN`): `docs/tools/analysis.md`.
- **Trivial functions count as done, proven by their bytes** (user
  decision 2026-11-09; `tools/trivial.py`, rules in
  `docs/tools/analysis.md`): status `identified`, never `verified`.
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
  large or collides with work in progress, add it to the backlog instead of
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
   `game/enums/` for jobs, types, goods and races (via
   `game/typed_fields.h`), named constants, decimal literals, helpers named
   after what each block of the original does, comments with addresses.
3. Write differential tests with `S3RE_TEST` in the same file. Run the
   original (through the `Original_*` trampoline) and our version on identical
   random inputs and starting state, then compare return values and all
   memory touched (including guard bytes past buffers). Where the comparison
   is non-trivial, also add a test that a deliberately wrong implementation
   is caught.
4. Add a spec to `tools/emutest/specs.py` and run `python
   tools/emutest/run.py [filter] --require-coverage` (Unicorn emulator:
   the original bytes against our function on identical random inputs;
   0 mismatches and every block covered is the gate). Read
   `docs/tools/emulator.md` before writing a spec (presets, virtuals,
   callees, `ignore_bytes`, ...). **Policy:** small and medium functions
   get full emulator evidence; large ones (GenerateRandomMap,
   UpdateSettlers, UpdateSoldier) get bounded runs and rely on the
   in-process tests and whole-game replays. Per batch: `run.py --changed
   --require-coverage` (quick mode, Release build by default). At the end
   of each parallel round and before milestones: the full suite with
   `--full`. `--self-test` when the harness changed. Our replacements
   must not rely on DLL startup code; call the game's operator
   new/delete/memmove (0x61efb9/0x61e4ef/0x61e860) where the original
   does.
5. Build, then run `s3re_launcher --test [filter]` (substrings, several
   separated by `|`, e.g. `"Fog|Sight"`). This runs the tests inside
   the real `S3.EXE` without opening the game window. The exit code is 0 only
   if every test passed. A crashing test is logged with the faulting
   instruction as module+offset (look the s3re.dll offset up in
   `s3re.map`, preferred base 0x10000000) and the address accessed.
6. Whole-game check (at milestones, not per batch; it's slow):
   `python tools/check_replay.py --recording <file> --against-original
   --ticks 3000` (and `check_save.py`, `check_random_map.py`); the traces
   must be identical. Record the call counts
   (`tools/record_call_counts.py`); never claim whole-game evidence for an
   uncalled function. Details: `docs/tools/runtime_checks.md`.
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
