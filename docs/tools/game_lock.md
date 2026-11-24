# Game lock and worktrees

Moved out of `CLAUDE.md` on 2026-11-24.

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
