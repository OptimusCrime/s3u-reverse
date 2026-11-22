# Settlers 3 Reverse

A reverse-engineering project that reimplements *The Settlers 3 – Ultimate
Collection* (Blue Byte, 1998) in modern C++, one function at a time, and
proves that each piece behaves exactly like the original.

**You need your own copy of the game.** It is sold DRM-free on GOG:
[The Settlers 3: Ultimate Collection](https://www.gog.com/en/game/the_settlers_3_ultimate_collection).

## What this is

- A **reimplementation** of the game's code, written by hand from studying
  how the original executable behaves.
- A **test bed**: every reimplemented function is compared against the
  original game code, on the same inputs, before it counts as done.
- Goals:
  1. Reimplement the game so it runs on Windows exactly like the original
     (current phase).
  2. Later, port it to macOS (and other platforms).

## What this is not

- **Not a copy of the game.** The repository contains no original game
  files: no executables, graphics, sounds, music, maps or texts beyond short
  excerpts needed to document the code. Nothing here is playable without
  the original game.
- **Not a crack or a piracy tool.** The launcher only accepts the GOG build
  of `S3.EXE` (checked by its SHA-256) and never modifies your installation.
- **Not decompiler output.** We study the original with disassemblers and
  decompilers, but the committed code is written by hand, with our own
  names, structure and comments.
- **Not affiliated with or endorsed by** Ubisoft, Blue Byte or GOG.
  *The Settlers* is a trademark of Ubisoft Entertainment.
- **Not finished.** Most of the game still runs as original code; see
  [Progress](#progress).

## How it works

`s3re_launcher.exe` starts your original `S3.EXE` and loads `s3re.dll`
into it (with [Microsoft Detours](https://github.com/microsoft/Detours)).
The DLL redirects original game functions to their reimplementations, so
the game stays playable at every step while more and more of it runs our
code. When nothing original is left, the same code will become a
standalone executable, and the DirectX parts will be replaced by a
cross-platform layer for the macOS port.

Each replacement is verified in three ways:

1. **Differential tests** inside the real game process: the original
   function and ours run on identical random inputs, and return values and
   all touched memory are compared byte for byte.
2. **Emulator tests**: the original machine code and ours run in an x86
   emulator on identical inputs; besides comparing results, this measures
   that every branch of the original was exercised.
3. **Whole-game checks**: the simulation is deterministic, so recorded
   games and savegames are replayed with the original code and with ours,
   and a hash of the game state must match on every tick.

## Building (Windows)

Requirements: Visual Studio 2026 with the "Desktop development with C++"
workload (CMake comes bundled with it), and Python 3 for the tools.

Open a "Developer PowerShell for VS" (or any terminal with CMake on the
PATH) in the repository root and run:

```
cmake --preset x86
cmake --build --preset debug
```

The first configure downloads Microsoft Detours.

## Running

Install the game from GOG, then:

```
build\x86\bin\Debug\s3re_launcher.exe [--game-dir <path to the game folder>]
```

The game folder defaults to `Settlers3Ultimate/` in the repository root
(gitignored). You can override it with `--game-dir` or the
`S3RE_GAME_DIR` environment variable. The DLL writes `s3re.log` next to
itself.

## Testing

```
build\x86\bin\Debug\s3re_launcher.exe --test [name filter]   # in-process differential tests
pip install -r tools/requirements.txt
python tools/emutest/run.py --require-coverage               # emulator tests
python tools/check_replay.py --help                          # whole-game checks
```

## Progress

`re/functions.csv` records every function identified so far and whether it
has been reimplemented and verified. Show a summary with:

```
python tools/inventory.py stats
```

The treemap below shows every function in `S3.EXE` as a box, sized by its
code size: grey = not looked at, red = classified, yellow = purpose known,
green = reimplemented and verified. `docs/progress.html` is the interactive
version (hover details, click to zoom, totals, history): open it in a
browser, or publish it together with `docs/progress-data.js`, the data file
it reads. Regenerate the data and the picture with `python tools/progress.py`.

![Reimplementation progress](docs/progress.svg)

## Repository layout

- `src/launcher/`: the launcher (checks `S3.EXE`, starts it with the DLL).
- `src/s3re/`: the DLL: reimplemented game code (`game/`), MSVC 6 and
  Watcom library code the game depends on, the test harness, and the
  record/replay and determinism tooling.
- `re/`: reverse-engineering notes and data: the function inventory,
  structure layouts, globals, analysis of larger subsystems.
- `tools/`: inventory, progress, emulator tests and whole-game checks.
- `docs/`: progress page and the [coding style](docs/coding_style.md).
- `CLAUDE.md`, `LOGBOOK.md`, `TODO.md`: project facts and procedures,
  progress log, backlog.

## License

Copyright (C) 2026 the Settlers 3 Reverse contributors.

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version. See [LICENSE](LICENSE).

The license covers this repository's own code and documentation only. The
original game, its code and its assets remain the property of their
owners and are not part of this project.
