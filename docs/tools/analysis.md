# Static analysis: disassembly, inventory, frontier, RTTI, source files

Moved out of `CLAUDE.md` on 2026-11-24.

## Disassembly and helpers

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
  finds them mechanically by decoding S3.EXE's bytes with capstone from
  each start (not the dumpbin listing, which jump tables desync: it once
  read 0x53ba50 `mov eax,[g_app]; ret` as `ret`; `check` also requires
  each note to quote exactly the decoded instructions): the whole body
  (start to next start, then only
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

## Function inventory

- `re/ghidra_functions.tsv` lists the 8,863 function starts Ghidra finds.
  It also keeps the 11,817 `Unwind@`/`Catch@` exception-cleanup snippets,
  with kind `funclet`, which are never counted. Regenerate it with
  `tools/import_ghidra_functions.py` from an REA evidence bundle
  (instructions in the script). Together with `re/extra_functions.tsv`, that
  makes 9,837 known functions.
- `re/functions.csv` holds one row per function we have identified. The
  columns are documented in `tools/inventory.py`. Names use `Class::Method`.
  The `source_file` column is generated (never edited by hand) by `python
  tools/source_files.py apply` from `re/source_files.csv`: the original
  file (`Volker/GodlyEvents.cpp`), `unnamed: A.cpp..B.cpp` between two
  named ranges, or empty for library code and the CRT. `inventory.py
  check` fails when it is out of date: rerun `apply` after `source_files.py
  scan` and after tools that add rows (`rtti.py apply`, `merge_survey.py`).
  The ranges are estimates (`re/source_files.md`, "Limits").
- Ghidra names made through REA are lost when the session closes. At the
  start of a session, re-apply them from `python tools/inventory.py names`
  with `set_addresses_names`.
- `python tools/inventory.py check` must pass before committing. It checks
  every hook in `src/` against its inventory row, that the tests named in
  `verified_by` exist, and that `re/whole_game_calls.csv` matches the
  inventory (after a rename: `python tools/whole_game.py fix-names`).

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
- Our source tree follows subsystems, not these files (decided
  2026-11-23, `docs/source_layout_plan.md`); the original file of each
  function is the `source_file` column of `re/functions.csv`.

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
