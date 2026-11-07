# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Find the game loop and the simulation tick.**

Goal: understand how the game is structured at runtime (threads, main loop,
where the simulation advances one step), name the functions along the way,
and get to the point where we can test whether the simulation is
deterministic.

Next steps:
1. Follow `WinMain` (`0x45a120`) downwards: application object creation,
   `DirectX::CGameApp` / `Win32::CApplication` startup, the main window
   `CSiedlerMainWindow`.
2. Find where the threads start. Classes `CLogicThread`, `CDrawThread`,
   `CNetThread` and `CConsoleThread` exist (see `re/classes.md`). The logic
   thread probably drives the simulation.
3. Locate the simulation tick (the function called once per game step) and
   the random number generator it uses (not CRT `rand`, which has only 28
   callers).
4. Record findings in `re/functions.csv` (rename `classified` rows to real
   names and set them to `identified`).

## Open questions and loose ends

- Constructors aren't named. `re/classes.json` lists, per vtable, the
  functions that store it (`writers`); separate constructors from
  destructors and inlined code.
- 84 virtual functions are shared by unrelated classes (linker folding); only
  `_purecall` and the empty `NoOp_retN` ones are named.
- The exact boundary between game code and the C runtime in `.text` is
  unknown (somewhere around `0x61e4ef`).
- The core simulation (settlers, buildings, map) has no polymorphic classes,
  so it has to be mapped through the call graph, strings and data references.
- Unconfirmed: is the simulation deterministic (lockstep multiplayer)?
- `CThreadData`, `CTrace` and `CTextException` are categorized as game
  classes by default; they might be library code.
- REA can't create functions in Ghidra, so the 973 functions in
  `re/extra_functions.tsv` can't be decompiled. Use `inspect_with_objdump`.

## Session log

### 2026-11-07: project setup, tooling, class structure

- Set up the repo (gitignore excludes the game install; LF line endings via
  `.gitattributes`), GitHub remote `origin`, branch `main`.
- Confirmed REA works (Ghidra 12.1.4 backend). Ghidra's analysis of `S3.EXE`
  takes about 2–3 minutes per session.
- Analyzed the binaries: `S3.EXE` and `S3_multi.EXE` differ in 7 bytes (GOG
  serial-number patches). MSVC 6.0, C++, fixed base `0x400000`.
- Chose the approach: incremental replacement via an injected DLL, with
  differential testing (see `CLAUDE.md`).
- Built `s3re_launcher.exe` + `s3re.dll` (Detours). Verified in a real run:
  the `WinMain` pass-through hook works and the game exits cleanly.
- Built the function inventory (`re/functions.csv`, `tools/inventory.py`)
  and the differential test harness (`s3re_launcher --test`). First verified
  function: `std::string::_Eos` (`0x401f60`), with 100,000 random cases.
- Recovered the class structure from RTTI (`tools/rtti.py`): 586 classes,
  1,376 virtual functions, 210 deleting destructors, 649 thunks; 973
  functions Ghidra missed. The inventory went from 5 to 1,486 named
  functions out of 9,836 known.
- Waiting on the user: they haven't yet played a game with the `_Eos`
  replacement active (integration check).
