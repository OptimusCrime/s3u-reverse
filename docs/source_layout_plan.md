# Plan: organising the source tree

Status: **proposal, nothing moved yet** (2026-11-23). This document answers
the roadmap item "reorganize our source tree after the original files"
(`LOGBOOK.md`, Roadmap) and the TODO items "Code organisation" and "Split
`game/enums.h` by category". It needs the user's decisions (section 6)
before any file moves.

## 1. Summary

- Keep `src/s3re/game/` as the root of all game code and keep the shared
  headers (`layout.h`, `globals.h`, `tables.h`, `texts.h`, `vtables.h`,
  `enums.h`, ...) where they are, so most `#include` lines don't change.
- Move the 114 other files of `game/` into **subsystem folders**
  (`settlers/`, `buildings/`, `economy/`, `map/`, `spells/`, `ai/`, `ui/`,
  `framework/`, ...). File names stay the same; only folders change.
- Don't use the original authors' folders (`Volker/`, `Dirk/`, `Rainer/`)
  as our folders: half of our hooks are in original files whose names we
  don't know, and many of our files mix several original files. Instead,
  record the original file of every function as data (a `source_file`
  column in `re/functions.csv`), and use it in comments where it's known.
- Split `enums.h` into `game/enums/*.h`, one header per category, with
  `enums.h` kept as an umbrella include.
- Do the whole move in **one commit**, made by a script from the table in
  the appendix, at a moment when no other worktree has unmerged work.

## 2. Current state

Numbers from a script over `src/s3re` (2026-11-23, commit `5d41bcb`).
"Hooks" counts `S3RE_HOOK*`/`S3RE_PASSTHROUGH` registrations, "tests"
counts `S3RE_TEST` blocks.

| Folder | Files (.cpp / .h) | Lines | Hooks | Tests |
|---|---|---|---|---|
| `src/s3re/` (DLL main, hooks, logs) | 8 (5 / 3) | 867 | 2 | 0 |
| `src/s3re/game/` | **126 (100 / 26)** | **70,500** | 588 | 556 |
| `src/s3re/msvc6/` (MSVC 6.0 `std::string`, exceptions) | 3 | 1,790 | 39 | 10 |
| `src/s3re/watcom/` (Watcom containers) | 3 | 539 | 4 | 4 |
| `src/s3re/test/` (test harness) | 6 | 794 | 0 | 3 |
| `src/s3re/tooling/` | 4 | 488 | 3 | 0 |
| `src/s3re/trace/` | 2 | 498 | 0 | 0 |
| `src/s3re/replay/` | 2 | 641 | 1 | 4 |
| **Total** | 154 | 76,117 | 637 | 579 |

(Plus `src/launcher/launcher.cpp` and `src/common/recording_format.h`,
which this plan doesn't touch.)

So 93 % of the code is in one flat folder. Grouped by subsystem (the
grouping proposed in section 4), `game/` holds:

| Subsystem | Files | Lines |
|---|---|---|
| Settlers and their jobs (UpdateSettlers) | 18 | 14,243 |
| Map, areas, territory, fog of war | 13 | 7,510 |
| Logic thread, commands (ExecuteCommands), game end | 17 | 10,242 |
| Buildings | 6 | 4,962 |
| Economy and goods piles (UpdateEconomy) | 6 | 4,855 |
| Framework (Win32/DirectX classes, strings, WinMain) | 10 | 3,949 |
| Spells (god events) | 6 | 3,599 |
| Computer players (AI) | 7 | 3,567 |
| World basics (RNG, players, statistics, tables) | 11 | 3,100 |
| Environment, map effects (UpdateEnvironment) | 4 | 2,953 |
| Shared headers (layout, globals, enums, ...) | 12 | 2,823 |
| Pathfinder | 2 | 2,502 |
| UI controls (CScreenElement) and menu layouts | 5 | 2,194 |
| Random map generator | 2 | 1,908 |
| Graphics and DirectDraw | 4 | 1,377 |
| Network thread, lobby buffer | 2 | 606 |

### Where our code sits in the original's files

For every hook address, `re/source_files.csv` (the method of
`re/source_files.md`) says which original file it lies in, or between
which two named files. Over all 637 hooks:

| Original | Hooks |
|---|---|
| Unnamed files (between two named ranges) | **327** |
| `Dirk/` | 133 |
| `Volker/` | 105 |
| `Rainer/` | 42 |
| `DirectX/`, `Win32/`, `Credits/` | 30 |

Things that matter for the layout:

- **Most of the simulation is in unnamed files.** All of buildings,
  economy, piles, settlers, map and random map generation lie in the two
  gaps `DrawBuilding.cpp .. GameDataSave.cpp` and
  `GameDataSave.cpp .. LogicDraw.cpp`. We can't name those files, only
  say "somewhere between these two".
- **Some of our files map well onto one original file:** `pathing.cpp`
  (Dirk/Pathing.cpp, 20 of 22 hooks), the spells files
  (Volker/GodlyEvents.cpp), `map_effects*.cpp` (Volker/LogicInit.cpp),
  `screen_element.cpp` and `ui_static_text.cpp` (Dirk/dialog.cpp),
  `ai_census.cpp` / `ai_evaland.cpp` (Rainer/Cevaland.cpp),
  `win32_errors.cpp` (Win32/Classes.cpp), `lobby_memory_block.cpp`
  (Dirk/protocol.cpp).
- **Others mix many original files**, because they were cut by kind of
  function ("small members", "string members"): `framework_small.cpp`
  (12 original files or gaps), `framework_helpers.cpp` (12),
  `string_members.cpp` (11), `ai_helpers.cpp` (5), `misc_helpers.cpp` (4).
- **Some original files are spread over many of ours:**
  Volker/GodlyEvents.cpp over 5 spell files, Volker/LogicInit.cpp over
  `map_effects*.cpp`, `spells.cpp`, `spell_effects.cpp`, `logic_thread.cpp`,
  `ui_state.cpp` and others. Our files follow what the code does, which is
  usually more useful to us than the original cut.

## 3. What depends on file paths

Checked tool by tool, so the move doesn't break anything silently.

| What | Depends on paths? | Effect of the move |
|---|---|---|
| `src/s3re/CMakeLists.txt` | Yes: lists every `.cpp` explicitly (no globbing) | Rewrite the list (group it by folder). |
| `#include` lines | Yes: all are relative to `src/s3re` (`"game/globals.h"`, `"test/difftest.h"`); none use `../` | Headers that stay in `game/` keep their include lines (the 12 shared headers are behind about 370 of the 769 include lines, `hooks.h` and the `test/` headers behind another 280). Only headers that move (e.g. `game/pathing.h`, `game/ai_layout.h`, `game/settler_jobs.h`, `trace/determinism.h`) need their includes rewritten: mechanical. |
| `tools/inventory.py`, `tools/whole_game.py`, `record_call_counts.py`, `savegame_calls.py` | No: they search `src/` recursively for `S3RE_HOOK`/`S3RE_TEST` | Nothing to do. |
| `tools/typed_access_lint.py` (`ALLOWED`, `GLOBAL_HEADERS`), `tools/raw_access_report.py` (`SKIP`) | Yes: sets of paths such as `game/layout.h`, `game/globals.h` | Unchanged if those headers stay in `game/` (they do in this plan). If the new `game/enums/*.h` should be exempt from anything, add them. |
| Emulator specs (`tools/emutest/specs.py`) | No: specs name hooks and symbols; `emu.py` finds our functions by name in `s3re.map` | Comments mentioning `src/s3re/...` paths (4) should be updated. |
| `tools/emutest/changed.py` (selection by compiled code) | Partly: it reads MSBuild's tracking logs and keys **object files by base name** (`pathing.obj`) | Works as long as **every `.cpp` base name stays unique** in the DLL (the plan keeps names; the mapping script checks it). In `--since`/`--by-headers` mode, a change to `src/s3re/CMakeLists.txt` selects all specs (fine: the move gets a full run anyway). |
| Fingerprints of compiled code | Possibly: symbols in anonymous namespaces (114 files use them) are mangled with `?A0x<hash>`, and MSVC derives that hash from the source file, probably its path | The first `run.py --changed` after the move may select most specs. Plan a full `--full` suite on the move commit anyway. |
| `__FILE__` in test failure messages | Yes, but only text | Failure messages show the new path. Harmless. |
| In-process test filter (`s3re_launcher --test NAME`) | No: by test name | Nothing to do. |
| Docs and data: `re/functions.csv` (550 `src/s3re/...` paths in notes), `re/globals.csv` (228 full paths, 300 bare file names), `CLAUDE.md` (12 full + 9 folder-relative), `re/*.md`, `docs/coding_style.md`, `tools/*.py` help texts | Yes, as text | Full and folder-relative paths are rewritten by the same script (old -> new path from the table). **Bare file names (`pathing.cpp`) stay valid** because names don't change: one more reason not to rename files in the move. |
| `LOGBOOK.md` session log | Text, historical | Leave old entries as they are (they describe the past). The new entry explains the move and points here. |
| Git history | `git mv` keeps blame/log across the move | Keep the move commit free of other edits (only paths and include lines), so git detects every rename (`git log --follow`, `git blame` work). |

## 4. Options

### (a) Folders by original author and file

```
game/Volker/GodlyEvents/ (or GodlyEvents.cpp), Volker/LogicInit.cpp, ...
game/Dirk/Pathing.cpp, Dirk/dialog.cpp, ...
game/Rainer/Cevaland.cpp, ...
game/Volker/unnamed_4c85a0/ ...
```

- For: mirrors the original exactly; "where would this function go" is
  answered by `tools/source_files.py which ADDR`, no judgement needed.
  Might help a future matching effort.
- Against: **327 of 637 hooks (51 %) are in unnamed files**, including
  almost all of the simulation, so the biggest folder would be
  "unnamed between DrawBuilding.cpp and GameDataSave.cpp", which tells a
  reader nothing. The ranges are estimates (`re/source_files.md`, "Limits":
  ranges can be too short, unnamed files can sit inside a named range,
  inline functions vote for the wrong file), so functions might have to
  move again as the ranges improve. Files like `string_members.cpp` would
  have to be split into 11 pieces. The author is not a useful category for
  someone looking for "the settler code". And for the standalone build and
  the macOS port, the line that matters is *simulation vs platform code*,
  which this layout doesn't show (Dirk wrote both the pathfinder and the
  DirectPlay lobby).

### (b) Folders by subsystem

```
game/settlers/, game/buildings/, game/economy/, game/map/, game/spells/,
game/ai/, game/ui/, game/graphics/, game/framework/, ...
```

- For: matches how we think and search ("the soldier code"), and how our
  files are already cut. Separates the portable simulation from
  Windows/DirectX code, which is what the standalone build and the macOS
  port need (section 4.2). Doesn't depend on estimated ranges.
- Against: needs judgement for files on a border (e.g. `ui_state.cpp`: UI
  state inside the world, called by the logic thread). Loses the link to
  the original file, unless we record it elsewhere.

### (c) Hybrid: subsystem folders, original names where known

Subsystem folders as in (b), plus:

- Every function's original file goes into `re/functions.csv` as data (a
  `source_file` column, section 5, step 6), so a script can show e.g. all
  verified functions of Volker/GodlyEvents.cpp.
- File header comments name the original file where it's known (most of
  them already do, e.g. `pathing.cpp`, `spells.cpp`).
- Where one of our files equals an original file, its folder may carry the
  original's name in a comment, but we don't rename our files to the
  original's names: names like `Cevaland.cpp` or `dialog.cpp` say less than
  `ai_census.cpp` or `screen_element.cpp`, and renaming would break the
  hundreds of bare file-name references in our notes (about 300 in `re/globals.csv` alone).

**Recommended: (c).** It costs no more than (b), keeps the useful part of
(a) as data that can be queried and corrected, and can't go stale when the
ranges improve (the column is regenerated, files don't move).

## 4.1 Target tree

Outline (folders only; every file is listed in the appendix):

```
src/s3re/
  dllmain.cpp hooks.{h,cpp} log.{h,cpp} crash_log.{h,cpp} debug_log.cpp
  CMakeLists.txt s3re.def                     (unchanged)
  game/                                       shared headers, unchanged paths:
    world.h layout.h globals.h global_access.h tables.h texts.h vtables.h
    raw.h typed_fields.h replacements.h debug_log.h enums.h (umbrella)
    enums/          one header per category (see 4.3)
    -- simulation (portable; Volker's logic and Dirk's pathfinder) --
    world/          CGameData basics: RNG, players, statistics, counters,
                    id allocators, small tables, notifications
    map/            tiles, directions, areas and regions, territory,
                    flood fills, fog of war, minimap
    mapgen/         the random map generator (GenerateRandomMap) and its
                    feature scattering
    environment/    UpdateEnvironment and the map effects
    economy/        UpdateEconomy, goods piles and their moves
    buildings/      sites, construction, crushing, flags, goods
    settlers/       UpdateSettlers, settler records, walking, soldiers
      jobs/         the job handlers of UpdateSettlers (settler_jobs_*.cpp)
    pathing/        the pathfinder (Dirk/Pathing.cpp)
    spells/         the god events (Volker/GodlyEvents.cpp)
    logic/          CLogicThread: Step, game end, victory conditions,
                    objectives, UI state in the world
      commands/     ExecuteCommands and its parts
    ai/             computer players (Rainer/*.cpp)
    -- platform side (Windows, DirectX, UI) --
    ui/             CScreenElement and the controls, menu/settings layouts
    graphics/       graphics data, landscape colours, DirectDraw surface
    net/            CNetThread, lobby protocol buffer
    framework/      WinMain, Win32/DirectX framework classes, exceptions,
                    string members, folded getters, misc helpers
  msvc6/            MSVC 6.0 library code (unchanged)
  watcom/           Watcom container library code (unchanged)
  test/             the differential test harness (unchanged)
  tooling/          launcher features: --fast, --random-map, --load-save
    trace/          the determinism trace (was src/s3re/trace/)
    replay/         command recorder and replay (was src/s3re/replay/)
```

Notes on the choices:

- **`game/` stays the root**, and the shared headers stay in it. That keeps
  about half of all include lines unchanged and keeps the lint's path sets
  valid. Subsystem-specific headers move with their code
  (`game/ai/ai_layout.h`, `game/logic/logic_thread_layout.h`,
  `game/ui/screen_element_layout.h`, `game/graphics/gfx_layout.h`,
  `game/pathing/pathing.h`, `game/settlers/jobs/settler_jobs.h`, ...).
- **At most three levels** below `src/s3re` (`game/settlers/jobs/`), so
  paths stay short.
- **The simulation/platform split is visible** in the folder list, which
  is what the standalone build and the macOS port need: the simulation
  folders should eventually not include Windows headers; `ui/`,
  `graphics/`, `net/` and `framework/` are what gets replaced (DirectX ->
  SDL). Later, if useful, the simulation folders could become their own
  CMake library target (`s3re_sim`) that the DLL and a standalone
  executable both link. Not part of this plan.
- **`tooling/` gets `trace/` and `replay/`** because they are all "original
  mode" tooling (the code that `S3RE_ORIGINAL_TOOLING_VERSION` covers), and
  `savegame_start.cpp` (the `--load-save` feature, now in `game/`) joins
  them. None of it belongs in a standalone game.
- **`msvc6/`, `watcom/` and `test/` stay** where they are: they're already
  separate, and moving them only costs include changes.
- **No file is renamed in the move.** Renames that would read better
  (e.g. `world_create.cpp` -> `random_map.cpp`, which holds
  GenerateRandomMap; `logic_helpers.cpp` / `misc_helpers.cpp` /
  `framework_small.cpp`, which are "batch N leftovers") are separate,
  later commits, done like any rename in `CLAUDE.md`.
- **Border cases** (decided by where the code's callers and data are):
  `ui_state.cpp` and `ui_overlays.cpp` go to `logic/` (UI state that lives
  in the world, read and written by the logic thread); `notifications.cpp`
  to `world/`; `minimap.cpp` to `map/` (computed by UpdateEnvironment from
  the tiles); `miner_jobs.cpp` to `settlers/` (callees of the job
  handlers); `directx_surface.cpp` to `graphics/`; `winmain.cpp` to
  `framework/`; `settler_groups.h` to `settlers/`.

## 4.2 Rules going forward

- New files go into the subsystem folder of their main function. A new
  subsystem folder needs a line in `CLAUDE.md` ("Code layout").
- `.cpp` base names stay unique across `src/s3re` (needed by
  `tools/emutest/changed.py`, and avoids object-file clashes). The
  migration script checks it; `inventory.py check` could check it too.
- Includes stay relative to `src/s3re` (`"game/settlers/jobs/settler_jobs.h"`),
  never `../`.
- Avoid catch-all files ("small helpers of batch N"): put a function next
  to its subsystem, even in a small file.

## 4.3 Splitting `enums.h`

`enums.h` (788 lines, included by 42 files) becomes a folder of category
headers. Its current sections map directly:

| New header | Contents (current section of `enums.h`) | Lines (approx.) |
|---|---|---|
| `game/enums/value.h` | `Value(E)` helper used by all enums | 10 |
| `game/enums/races.h` | `Race`, `kRaces` | 12 |
| `game/enums/settler_types.h` | `SettlerType`, `kSettlerTypeCount`, `SettlerClass` | 75 |
| `game/enums/buildings.h` | `BuildingType`, `kBuildingTypeCount` | 65 |
| `game/enums/goods.h` | `Good`, `kGoodCount` | 45 |
| `game/enums/piles.h` | `PileKind`, `kLastStoragePileKind` | 15 |
| `game/enums/spells.h` | `Spell`, `kSpellCount` | 45 |
| `game/enums/settler_jobs.h` | `SettlerJob`, `kSettlerJobCount` (the 400-line block) | 410 |
| `game/enums/commands.h` | `NetMessage`, `CommandId` | 70 |
| `game/enums/terrain.h` | terrain classes, water/shore constants | 10 |
| `game/enums/directions.h` | `kDirectionCount` and other grid constants | 5 |

- `game/enums.h` stays as an umbrella that includes all of them, so nothing
  breaks. Files then switch to the headers they need (one by one, or in a
  later sweep); `typed_fields.h` includes only the headers of the fields it
  types.
- New categories (map objects, tile flags, ... from `re/enums.md`) get
  their own header there.
- `game/enums/settler_jobs.h` vs `game/settlers/jobs/settler_jobs.h`: two
  headers with the same base name in different folders are fine for the
  compiler (only `.cpp` names must be unique), but to avoid confusion the
  enum header could be `job_ids.h`. (Open question 4.)
- Do the split as **its own commit**, before or after the move (not in
  it), so the move commit stays pure renames.

## 5. Migration steps

1. **Agree on the plan** (section 6), then update the appendix if anything
   changes.
2. **Pick a quiet moment.** The move touches almost every file, so any
   unmerged branch would conflict on every file it touched. Merge or
   finish all worktrees first (`git worktree list`; on 2026-11-23 there
   are four besides main: ai-a2, ai-a3, cleanups, layout-plan). During the
   move no other agent works. Afterwards, an old branch can still be
   rebased: git follows renames when merging, so edits inside moved files
   usually merge; new files and new includes need fixing by hand.
3. **Write `tools/move_sources.py`** (committed; it's the record of the
   move) from the appendix table. It:
   - checks that every file in `src/s3re` is mapped and that `.cpp` base
     names stay unique;
   - runs `git mv` for each moved file;
   - rewrites `#include "old/path.h"` to the new path in `src/`;
   - rewrites the source list in `src/s3re/CMakeLists.txt` (grouped by
     folder, with a comment per group);
   - rewrites full and folder-relative paths (`src/s3re/game/pathing.cpp`,
     `game/pathing.h`) in `CLAUDE.md`, `re/*.csv`, `re/*.md`, `docs/*.md`,
     `tools/**/*.py` and `TODO.md` (not `LOGBOOK.md`'s old entries);
   - has a `--dry-run` that prints all of it.
4. **One move commit:** run the script, then
   `cmake --preset x86`, `cmake --build --preset debug` and `--preset
   release`, `python tools/inventory.py check` (includes the lint),
   `s3re_launcher --test` (all in-process tests), and the full emulator
   suite `python tools/emutest/run.py --full --require-coverage` (code
   fingerprints may change through anonymous-namespace names, see
   section 3). Commit with only renames and path edits in it. No game run
   is needed: the compiled code is the same (a check: the Release DLL's
   `.text` should differ only where anonymous-namespace names or
   `__FILE__` strings differ).
5. **Update `CLAUDE.md`** ("Code layout": the folder list and the rules of
   4.2) in the same commit, and add a LOGBOOK entry.
6. **Optional, separate commits afterwards:**
   - split `enums.h` (4.3);
   - add the `source_file` column (below);
   - renames that read better, and splitting the catch-all files
     (`framework_small.cpp`, `misc_helpers.cpp`, `logic_helpers.cpp`,
     `ai_helpers.cpp`/`ai_small_helpers.cpp`) into their subsystems;
   - later, a CMake library target for the simulation folders.

### The `source_file` column in `re/functions.csv`

Worth it, as **generated data**:

- Values: `Volker/GodlyEvents.cpp` for a function inside a named range;
  `unnamed: DrawBuilding.cpp..GameDataSave.cpp` for one between two
  ranges; empty for the C runtime and library code (after `0x61e4ef`, and
  rows with category crt/stl/mfc/watcom/compiler).
- Filled by a new `python tools/source_files.py apply`, rerun after every
  `scan`; never edited by hand (the ranges are estimates, so the column
  must follow them). `inventory.py check` would verify that the column
  equals what `apply` would write, like `trivial.py check`.
- Uses: the progress page and `inventory.py stats` can count verified
  functions per original file; batch planning can pick "finish
  Volker/GodlyEvents.cpp"; reviewers see at a glance where a function sat.
- Cost: `inventory.py` has a fixed column list (`COLUMNS`) to extend; the
  tools that read the CSV use column names (`csv.DictReader`), but check
  `asm_tools.py`, `frontier.py`, `merge_survey.py`, `progress.py`,
  `record_call_counts.py`, `rtti.py`, `savegame_calls.py`,
  `survey_functions.py`, `trivial.py`, `whole_game.py` for positional
  access before adding it. About 10,000 rows change once (a big but
  mechanical diff; do it in its own commit).

## 6. Open questions for the user

1. **Hybrid layout (c)** as recommended, or strictly by original file (a)?
2. **Folder names:** OK with `world/` vs `map/` (world = the CGameData
   object's general state, map = the tile grid), `mapgen/` for the random
   map generator, and `logic/commands/` for ExecuteCommands?
3. **Move `trace/` and `replay/` under `tooling/`**, or leave them at the
   top level (fewer changes)?
4. **Enum header for jobs:** `game/enums/settler_jobs.h` (same name as the
   job-handler header in `game/settlers/jobs/`) or `game/enums/job_ids.h`?
5. **`source_file` column:** add it (generated, checked), or keep using
   `tools/source_files.py which` on demand?
6. **When:** after the current AI worktrees are merged? The move needs a
   moment when no other agent has unmerged work.

## Appendix: every file and its new place

Generated by a script (in the session's scratch area; the migration script
of step 3 will take the same table). 156 files, 118 of them move. "Now" is
the path relative to `src/s3re/` ("same" = doesn't move). The last column
says which original file(s) the file's hooks lie in, from
`re/source_files.csv`; "unnamed (A to B)" means between the named files A
and B. Files without hooks (job handler parts, headers) are parts of the
function named in their header comment.

#### `src/s3re/` (10 files, 867 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| CMakeLists.txt | same |  |  |
| s3re.def | same |  |  |
| dllmain.cpp | same |  |  |
| hooks.cpp | same |  |  |
| hooks.h | same | 1 | unnamed (start to Console.cpp) |
| log.cpp | same |  |  |
| log.h | same |  |  |
| crash_log.cpp | same |  |  |
| crash_log.h | same |  |  |
| debug_log.cpp | same | 1 | Dirk/dialog.cpp |

#### `src/s3re/game/` (12 files, 2,823 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| world.h | same |  |  |
| layout.h | same |  |  |
| globals.h | same |  |  |
| global_access.h | same |  |  |
| tables.h | same |  |  |
| texts.h | same |  |  |
| vtables.h | same |  |  |
| raw.h | same |  |  |
| typed_fields.h | same |  |  |
| replacements.h | same |  |  |
| debug_log.h | same |  |  |
| enums.h | same |  |  |

#### `src/s3re/msvc6/` (3 files, 1,790 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| exceptions.cpp | same | 5 | Rainer/Cevaland.cpp |
| string.cpp | same | 34 | unnamed (start to Console.cpp); Dirk/Console.cpp; Dirk/dialog.cpp; unnamed (DGlobals.cpp to dialog.cpp); unnamed (GameParams.cpp to LobbyDialogs.cpp); unnamed (Lobby.cpp to graphicsdata.cpp); Dirk/DGlobals.cpp; Dirk/LobbyDialogs.cpp |
| string.h | same |  |  |

#### `src/s3re/watcom/` (3 files, 539 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| ptr_vector.cpp | same | 3 | Dirk/dialog.cpp; unnamed (GameParams.cpp to LobbyDialogs.cpp) |
| ptr_vector.h | same |  |  |
| string_vector.cpp | same | 1 | Dirk/Console.cpp |

#### `src/s3re/test/` (6 files, 794 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| callee_stub.h | same |  |  |
| difftest.cpp | same |  |  |
| difftest.h | same |  |  |
| fake_graphics.h | same |  |  |
| harness_selftest.cpp | same |  |  |
| world_buffer.h | same |  |  |

#### `src/s3re/tooling/` (5 files, 598 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| fast.cpp | same | 1 | Dirk/MenuNetThread.cpp |
| random_map_start.cpp | same | 2 | Dirk/GameParams.cpp; Dirk/nethelp.cpp |
| random_map_start.h | same |  |  |
| version.h | same |  |  |
| savegame_start.cpp | game/savegame_start.cpp | 1 | Dirk/netdialogs.cpp |

#### `src/s3re/tooling/trace/` (2 files, 498 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| determinism.cpp | trace/determinism.cpp |  |  |
| determinism.h | trace/determinism.h |  |  |

#### `src/s3re/tooling/replay/` (2 files, 641 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| command_log.cpp | replay/command_log.cpp | 1 | Dirk/GameParams.cpp |
| command_log.h | replay/command_log.h |  |  |

#### `src/s3re/game/world/` (11 files, 3,100 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| random.cpp | game/random.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| random.h | game/random.h |  |  |
| players.cpp | game/players.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); Volker/GameDataSave.cpp |
| player_queries.cpp | game/player_queries.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| player_presence.cpp | game/player_presence.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| statistics.cpp | game/statistics.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| counting.cpp | game/counting.cpp | 7 | unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp |
| world_tables.cpp | game/world_tables.cpp | 11 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| id_allocators.cpp | game/id_allocators.cpp | 3 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| world_search.cpp | game/world_search.cpp | 2 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| notifications.cpp | game/notifications.cpp | 1 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |

#### `src/s3re/game/map/` (13 files, 7,510 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| map.cpp | game/map.cpp | 4 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_directions.cpp | game/map_directions.cpp | 5 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| map_neighbourhood.cpp | game/map_neighbourhood.cpp | 7 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_marks.cpp | game/map_marks.cpp | 3 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_terrain.cpp | game/map_terrain.cpp | 6 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_areas.cpp | game/map_areas.cpp | 5 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_regions.cpp | game/map_regions.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| area_split.cpp | game/area_split.cpp | 13 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| flood_fills.cpp | game/flood_fills.cpp | 14 | unnamed (GameDataSave.cpp to LogicDraw.cpp); unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| territory.cpp | game/territory.cpp | 4 | unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp; unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| terrain_updates.cpp | game/terrain_updates.cpp | 2 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| fog_of_war.cpp | game/fog_of_war.cpp | 5 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| minimap.cpp | game/minimap.cpp | 2 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |

#### `src/s3re/game/mapgen/` (2 files, 1,908 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| world_create.cpp | game/world_create.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| map_generation.cpp | game/map_generation.cpp | 7 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |

#### `src/s3re/game/environment/` (4 files, 2,953 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| environment.cpp | game/environment.cpp | 2 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| map_effects.cpp | game/map_effects.cpp | 30 | Volker/LogicInit.cpp |
| map_effect_spawns.cpp | game/map_effect_spawns.cpp | 10 | Volker/LogicInit.cpp |
| map_effect_expiry.cpp | game/map_effect_expiry.cpp | 2 | Volker/LogicInit.cpp |

#### `src/s3re/game/economy/` (6 files, 4,855 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| update_economy.cpp | game/update_economy.cpp | 1 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| economy.cpp | game/economy.cpp | 4 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp |
| economy_helpers.cpp | game/economy_helpers.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| goods_piles.cpp | game/goods_piles.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| area_piles.cpp | game/area_piles.cpp | 9 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| pile_goods_moves.cpp | game/pile_goods_moves.cpp | 4 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |

#### `src/s3re/game/buildings/` (6 files, 4,962 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| building_sites.cpp | game/building_sites.cpp | 5 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| building_done.cpp | game/building_done.cpp | 1 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| building_crush.cpp | game/building_crush.cpp | 10 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp |
| building_flags.cpp | game/building_flags.cpp | 8 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| building_goods.cpp | game/building_goods.cpp | 7 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| building_tiles.cpp | game/building_tiles.cpp | 4 | Volker/GameDataSave.cpp; unnamed (DrawBuilding.cpp to GameDataSave.cpp) |

#### `src/s3re/game/settlers/` (10 files, 6,579 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| update_settlers.cpp | game/update_settlers.cpp | 1 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| settlers.cpp | game/settlers.cpp | 2 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| settler_records.cpp | game/settler_records.cpp | 6 | unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp; unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| settler_queries.cpp | game/settler_queries.cpp | 9 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); Volker/GameDataSave.cpp |
| settler_spawn.cpp | game/settler_spawn.cpp | 5 | Volker/GameDataSave.cpp |
| settler_idle.cpp | game/settler_idle.cpp | 1 | Volker/GameDataSave.cpp |
| settler_walk.cpp | game/settler_walk.cpp | 4 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| soldier.cpp | game/soldier.cpp | 1 | Volker/GameDataSave.cpp |
| miner_jobs.cpp | game/miner_jobs.cpp | 4 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| settler_groups.h | game/settler_groups.h |  |  |

#### `src/s3re/game/settlers/jobs/` (8 files, 7,664 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| settler_jobs.h | game/settler_jobs.h |  |  |
| settler_jobs_test.h | game/settler_jobs_test.h |  |  |
| settler_jobs_units.cpp | game/settler_jobs_units.cpp |  |  |
| settler_jobs_building.cpp | game/settler_jobs_building.cpp |  |  |
| settler_jobs_metal.cpp | game/settler_jobs_metal.cpp |  |  |
| settler_jobs_food.cpp | game/settler_jobs_food.cpp |  |  |
| settler_jobs_drinks.cpp | game/settler_jobs_drinks.cpp |  |  |
| settler_jobs_special.cpp | game/settler_jobs_special.cpp |  |  |

#### `src/s3re/game/pathing/` (2 files, 2,502 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| pathing.cpp | game/pathing.cpp | 22 | Dirk/Pathing.cpp; unnamed (nethelp.cpp to Pathing.cpp) |
| pathing.h | game/pathing.h |  |  |

#### `src/s3re/game/spells/` (6 files, 3,599 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| godly_events.cpp | game/godly_events.cpp | 2 | unnamed (GodlyEvents.cpp to landscape.cpp); Volker/GodlyEvents.cpp |
| spells.cpp | game/spells.cpp | 7 | Volker/GodlyEvents.cpp; Volker/LogicInit.cpp |
| spells_land.cpp | game/spells_land.cpp | 8 | unnamed (LogicInit.cpp to GodlyEvents.cpp); Volker/GodlyEvents.cpp |
| spell_units.cpp | game/spell_units.cpp | 7 | Volker/GodlyEvents.cpp |
| spell_goods.cpp | game/spell_goods.cpp | 4 | Volker/GodlyEvents.cpp |
| spell_effects.cpp | game/spell_effects.cpp | 1 | Volker/LogicInit.cpp |

#### `src/s3re/game/logic/` (9 files, 4,649 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| logic_thread.cpp | game/logic_thread.cpp | 1 | Volker/LogicInit.cpp |
| logic_thread_layout.h | game/logic_thread_layout.h |  |  |
| logic_helpers.cpp | game/logic_helpers.cpp | 4 | unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/GameDataSave.cpp; Volker/LogicInit.cpp |
| game_end_check.cpp | game/game_end_check.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| game_end.cpp | game/game_end.cpp | 6 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| victory_conditions.cpp | game/victory_conditions.cpp | 1 | unnamed (GameDataSave.cpp to LogicDraw.cpp) |
| objectives.cpp | game/objectives.cpp | 3 | unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| ui_state.cpp | game/ui_state.cpp | 15 | unnamed (GameDataSave.cpp to LogicDraw.cpp); Volker/LogicInit.cpp; unnamed (DrawBuilding.cpp to GameDataSave.cpp) |
| ui_overlays.cpp | game/ui_overlays.cpp | 3 | Volker/LogicDraw.cpp |

#### `src/s3re/game/logic/commands/` (8 files, 5,593 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| execute_commands.cpp | game/execute_commands.cpp | 1 | Volker/LogicHandleNet.cpp |
| execute_commands.h | game/execute_commands.h |  |  |
| execute_commands_test.h | game/execute_commands_test.h |  |  |
| execute_commands_areas.cpp | game/execute_commands_areas.cpp |  |  |
| execute_commands_net.cpp | game/execute_commands_net.cpp |  |  |
| execute_commands_trade.cpp | game/execute_commands_trade.cpp |  |  |
| execute_commands_units.cpp | game/execute_commands_units.cpp |  |  |
| command_helpers.cpp | game/command_helpers.cpp | 8 | unnamed (DrawBuilding.cpp to GameDataSave.cpp); unnamed (GameDataSave.cpp to LogicDraw.cpp) |

#### `src/s3re/game/ai/` (7 files, 3,567 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| ai_layout.h | game/ai_layout.h |  |  |
| ai_computer_player.cpp | game/ai_computer_player.cpp | 19 | unnamed (landscape.cpp to Cevaland.cpp); unnamed (CompHouseReq.cpp to CompMapWorkerAgent.cpp) |
| ai_agents.cpp | game/ai_agents.cpp | 11 | unnamed (CompHouseReq.cpp to CompMapWorkerAgent.cpp); unnamed (CompDoBuildHouseReq.cpp to CompGExpand.cpp); Rainer/CompHouseReq.cpp; Rainer/Cevaland.cpp; Rainer/CompDoBuildHouseReq.cpp; unnamed (Cevaland.cpp to CompDoBuildHouseReq.cpp); Rainer/CompMapWorkerAgent.cpp; Rainer/compGlobs.cpp |
| ai_census.cpp | game/ai_census.cpp | 7 | Rainer/Cevaland.cpp |
| ai_evaland.cpp | game/ai_evaland.cpp | 2 | Rainer/Cevaland.cpp |
| ai_helpers.cpp | game/ai_helpers.cpp | 22 | Rainer/Cevaland.cpp; Rainer/CompMapWorkerAgent.cpp; unnamed (landscape.cpp to Cevaland.cpp); Rainer/compGlobs.cpp; unnamed (CompHouseReq.cpp to CompMapWorkerAgent.cpp) |
| ai_small_helpers.cpp | game/ai_small_helpers.cpp | 8 | Rainer/Cevaland.cpp; unnamed (landscape.cpp to Cevaland.cpp); Rainer/CompMapWorkerAgent.cpp |

#### `src/s3re/game/ui/` (5 files, 2,194 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| screen_element.cpp | game/screen_element.cpp | 62 | Dirk/dialog.cpp; unnamed (DGlobals.cpp to dialog.cpp) |
| screen_element_layout.h | game/screen_element_layout.h |  |  |
| ui_static_text.cpp | game/ui_static_text.cpp | 2 | Dirk/dialog.cpp |
| menus_layout.h | game/menus_layout.h |  |  |
| game_settings_layout.h | game/game_settings_layout.h |  |  |

#### `src/s3re/game/graphics/` (4 files, 1,377 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| graphics_helpers.cpp | game/graphics_helpers.cpp | 8 | Volker/DrawBuilding.cpp; Dirk/graphicsdata.cpp; unnamed (graphicsdata.cpp to SoundData.cpp); Volker/LogicInit.cpp |
| gfx_layout.h | game/gfx_layout.h |  |  |
| landscape_colours.cpp | game/landscape_colours.cpp | 1 | Volker/landscape.cpp |
| directx_surface.cpp | game/directx_surface.cpp | 10 | DirectX/netsession.cpp; Volker/landscape.cpp; Dirk/Console.cpp |

#### `src/s3re/game/net/` (2 files, 606 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| net_thread.cpp | game/net_thread.cpp | 1 | Dirk/Net.cpp |
| lobby_memory_block.cpp | game/lobby_memory_block.cpp | 12 | Dirk/protocol.cpp; unnamed (SoundData.cpp to protocol.cpp) |

#### `src/s3re/game/framework/` (10 files, 3,949 lines)

| File | Now | Hooks | Original source file(s) of its hooks |
|---|---|---|---|
| winmain.cpp | game/winmain.cpp | 1 | Dirk/LobbyManager.cpp |
| win32_classes.cpp | game/win32_classes.cpp | 11 | Win32/Classes.cpp; unnamed (protocol.cpp to Synchronize.cpp); DirectX/netsession.cpp; unnamed (Synchronize.cpp to synchronize.ipp); DirectX/Dplay.cpp |
| win32_errors.cpp | game/win32_errors.cpp | 6 | Win32/Classes.cpp |
| win32_exceptions.cpp | game/win32_exceptions.cpp | 4 | Win32/Classes.cpp; Dirk/SoundData.cpp |
| framework_helpers.cpp | game/framework_helpers.cpp | 15 | DirectX/netsession.cpp; Dirk/protocol.cpp; Dirk/Lobby.cpp; unnamed (netdialogs.cpp to nethelp.cpp); Dirk/lobbyclient.cpp; unnamed (SoundData.cpp to protocol.cpp); Credits/cpp_exo_text.cpp; unnamed (Internet.cpp to Mapping.cpp); unnamed (Ddraw.cpp to Dplay.cpp); unnamed (protocol.cpp to Synchronize.cpp); unnamed (synchronize.ipp to Classes.cpp); Dirk/nethelp.cpp |
| framework_small.cpp | game/framework_small.cpp | 17 | unnamed (protocol.cpp to Synchronize.cpp); unnamed (GameParams.cpp to LobbyDialogs.cpp); Dirk/dialog.cpp; Dirk/graphicsdata.cpp; unnamed (DGlobals.cpp to dialog.cpp); Dirk/LobbyManager.cpp; Dirk/protocol.cpp; unnamed (LobbyDialogs.cpp to LobbyManager.cpp); unnamed (SoundData.cpp to protocol.cpp); unnamed (cdaudio.cpp to Registry.cpp); Win32/Synchronize.cpp; Dirk/lobbyclient.cpp |
| framework_layout.h | game/framework_layout.h |  |  |
| folded_getters.cpp | game/folded_getters.cpp | 2 | Dirk/nethelp.cpp; Dirk/DGlobals.cpp |
| string_members.cpp | game/string_members.cpp | 18 | Dirk/LobbyManager.cpp; Dirk/ConsoleInit.cpp; Dirk/LobbyDialogs.cpp; Dirk/netdialogs.cpp; Dirk/Lobby.cpp; DirectX/netsession.cpp; unnamed (LobbyDialogs.cpp to LobbyManager.cpp); Dirk/MenuNetThread.cpp; Dirk/dialog.cpp; DirectX/Gamebase.cpp; unnamed (cdaudio.cpp to Registry.cpp) |
| misc_helpers.cpp | game/misc_helpers.cpp | 4 | unnamed (GameParams.cpp to LobbyDialogs.cpp); Dirk/LobbyManager.cpp; Dirk/protocol.cpp; Credits/cpp_exo_text__diverses.h |
