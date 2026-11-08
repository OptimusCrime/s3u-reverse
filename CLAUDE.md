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
  goes in `re/`. Progress goes in `LOGBOOK.md`.
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
    world->UpdatePhase2      (0x4e05e0)     unknown
    world->UpdateSettlers    (0x4e9670)     settler jobs (provisional)
    0x550f80 on 20 objects at this+0x350    per player? (unknown)
    game-end check 0x511c50, tick bookkeeping
```

- **The world object** (`GameWorld`, our name; the original class is
  `CGameData`, 0x153A128 bytes, vtable at +0) holds the whole simulation
  state. Known offsets used by our code are in `src/s3re/game/world.h`.
  The map is stored in rows of `0x300` tiles of 24 bytes; the map size is at
  `+0x2d2d0`.
- **Field knowledge lives in the world-layout files:** `re/world_layout.md`
  (memory map of the whole object, with confidence levels), `re/structs.md`
  (field tables per record: tile, area, pile, settler, building, player...)
  and `re/proposed_structs.h` (a proposed C++ header, not used by the build
  yet). `python tools/world_fields.py` lists every world displacement in
  `build/s3.asm` grouped by region and record field (lower bounds; check
  single fields by hand). New findings about fields go into
  `re/structs.md` / `re/world_layout.md`, not only into code comments.
- **The simulation's random number generator** is `GameWorld::Random`
  (`0x4feb70`), with its state at world `+0x2d2dc`. It's reimplemented and
  verified. Because the state lives in the world, the simulation is probably
  deterministic. It's seeded by `GameWorld::SeedRandom` (`0x4feb40`) at
  game start, with a different seed each game.
- **The simulation is deterministic** given the map, the RNG seed and (so
  far, no) player input, verified by unattended runs.
- **Launcher options for runtime checks:** `--trace-determinism` writes a
  per-tick trace (compare two with `tools/compare_traces.py`); `--seed N`
  forces the RNG seed; `--exit-after-ticks N` ends the game at tick N;
  `--timeout S` kills it after S seconds; `--dump-tick N` dumps the hashed
  world memory at tick N. Game options go after `--`, e.g.
  `-- -map Map\MULTI\384-2-Lakes.map` starts that map without menus.
  `s3re.log` logs every seed and the step count.
- **Unattended determinism check:** `python tools/check_determinism.py
  [--ticks N]`. It opens the game window, so the user's screen is busy
  while it runs (about 1 minute per 1,000 ticks per run).
- **Recording and replaying commands:** `--record-commands FILE` records
  every command; `--replay-commands FILE` replays a recording instead of the
  live commands. Recordings (format in `src/common/recording_format.h`)
  store the game seed, the map and the random-race setting; the launcher
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
  `recordings/scripted-seed127.bin` was recorded with seed 1234 (127 was
  the synthetic-input seed); it has been upgraded to version 2.
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
  makes 9,836 known functions.
- `re/functions.csv` holds one row per function we have identified. The
  columns are documented in `tools/inventory.py`. Names use `Class::Method`.
- Ghidra names made through REA are lost when the session closes. At the
  start of a session, re-apply them from `python tools/inventory.py names`
  with `set_addresses_names`.
- `python tools/inventory.py check` must pass before committing. It checks
  every hook in `src/` against its inventory row, and that the tests named in
  `verified_by` exist.
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
    Ghidra missed
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
   itself in original mode.
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
   otherwise keep their image values). `run.py --self-test` checks that the
   harness catches deliberately wrong pairings. Our replacements must not
   rely on DLL startup code (the emulator doesn't run it).
5. Build, then run `s3re_launcher --test [filter]`. This runs the tests inside
   the real `S3.EXE` without opening the game window. The exit code is 0 only
   if every test passed.
6. Whole-game check (at milestones, not per batch; it's slow):
   `python tools/check_replay.py --recording <file> --against-original
   --ticks 3000` replays a recorded game with the original code
   (`s3re_launcher --original` leaves out all replacements) and with ours,
   and the traces must be identical. It only covers functions the game
   actually called: replacements run behind a counting thunk, the counts
   are logged at exit ("calls <name> <n>" in s3re.log) and check_replay.py
   lists the ones that were never called. Record call counts in the
   inventory notes; never claim whole-game evidence for an uncalled
   function. Keep
   recordings with player input in `build/x86/bin/Debug/recordings/` (make
   one with `check_replay.py --synthetic-input SEED`).
7. Set the row to `verified`, `hook=replaced`, `verified_by=<test names>`
   (space-separated). Run `python tools/inventory.py check`.
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
