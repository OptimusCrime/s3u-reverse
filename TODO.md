# TODO (backlog)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"); larger directions are
in its "Roadmap" section; unexplained behaviour goes under "Open
questions". Add an item here whenever something is postponed, and remove
it when it's done (mention it in the LOGBOOK session entry).

Each item: what, why, and where to start.

## Naming

- **Rename `GameWorld::Create` (0x5166d0) to `CGameData::GenerateRandomMap`.**
  It is the random map generator ("Erstelle Zufallswelt"), not a general
  world constructor; file maps use 0x4fd540, savegames 0x508fa0.
  Postponed until the struct refactor (batch 2b) has landed.
- **Rename the `GameWorld` class to `CGameData` everywhere** (code,
  inventory names, docs, test names). `CGameData` is the real class name
  from RTTI. Large mechanical change; do it in one dedicated commit after
  the struct refactor.
- **Audit provisional names.** Many names were given early from partial
  evidence (e.g. `GameWorld::UpdateSettlers`, `GameWorld::UpdatePhase2`,
  `Logic_sub_*` / `SaveGame_sub_*` / `AI_sub_*` survey names). Review them
  when the functions are worked on; see the naming rule in `CLAUDE.md`.
- **Name constructors** (`re/classes.json` lists the functions that store
  each vtable).

## Evidence and testing

- **Record/replay of random-map games:** `--random-map` can't be combined
  with the command recorder, because recordings store only a map file
  name. Add the random-map parameters to the recording format (version 3)
  so synthetic-input games on random maps can be replayed.
- **Faster unattended runs (in progress in a parallel worktree, `--fast`):**
  every check runs the game in real time (about
  15 ticks/s). A tooling hook that removes the logic thread's pacing
  (and maybe skips drawing) would speed up every whole-game check.
  Start at CLogicThread / DirectX::CGameThread::Main 0x5b63d0.
- **Fair game lock:** tools/game_lock.py polls every 30 s with no queue, so
  one agent's series of checks can starve another's; it also waits for
  `--test` runs (any S3.EXE). A FIFO ticket directory and a shorter poll
  would help; `--test` runs could take a shared/readers lock.
- **Emulator coverage of `GameWorld::Create`:** 338/415 blocks; the rest
  needs maps of 130+ tiles, too slow in the emulator (a 320-tile case ran
  for over an hour). Options: a release build of s3re.dll for the emulator,
  a faster emulator path, or accept the in-process whole-world comparison.
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

## Known differences to clean up

- Some replacements leak temporaries on fatal error paths because they have
  no exception-unwind frames (std::string `_Xlen`/`_Xran` paths, the throw
  helpers 0x5ffcc0 / ThrowOutOfMemory, ...). Harmless today (the game
  ends), but worth fixing for the standalone build. List: inventory notes
  containing "Known difference".

## Code organisation

- **Reorganize our source tree after the original source files**
  (`re/source_files.md`; plan first, see the LOGBOOK roadmap).

## Postponed by decision

- **Network and lobby code** (CNetThread, Lobby::*, LobbyClient::*,
  DirectPlay, CLM* messages): skipped since 2026-11-09 (single player
  against computer players is easier to verify; online play may not matter
  today). Revisit when the single-player game is far along.
