#!/usr/bin/env python3
"""A lock for runs that start S3.EXE with a window.

Several agents (or worktrees) may launch the game; two games at once fight
over the screen, DirectDraw and the input. Every tool that starts a windowed
game takes this lock first:

    with game_lock.held("check_save"):
        subprocess.run([launcher, ...])

The lock is the directory <game dir>\\s3re-game.lock (created atomically
with mkdir, with owner.txt inside: owner and time). While it exists, or
while an S3.EXE is running, other runs wait (checking every 30 s). A lock
older than 2 hours with no S3.EXE running is stale and is removed. The lock
is removed when the run ends, also on errors.

The game directory is S3RE_GAME_DIR, or Settlers3Ultimate in the repository
(in a git worktree that is a junction to the main checkout's, so all
worktrees share one lock). `--test` runs don't open a window and don't need
the lock.

Usage as a script: python tools/game_lock.py status | release
"""

import contextlib
import datetime
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STALE_SECONDS = 2 * 3600
POLL_SECONDS = 30


def game_dir():
    return os.environ.get("S3RE_GAME_DIR") or os.path.join(ROOT, "Settlers3Ultimate")


def lock_path():
    return os.path.join(game_dir(), "s3re-game.lock")


def game_running():
    """True if an S3.EXE process exists."""
    result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq S3.EXE", "/NH"],
                            capture_output=True, text=True)
    return "s3.exe" in result.stdout.lower()


def _try_acquire(owner):
    path = lock_path()
    try:
        os.mkdir(path)
    except FileExistsError:
        age = time.time() - os.path.getmtime(path)
        if age > STALE_SECONDS and not game_running():
            print(f"game lock: removing stale lock ({age / 3600:.1f} h old)", flush=True)
            shutil.rmtree(path, ignore_errors=True)
        return False
    if game_running():
        # Someone runs the game without the lock; give it back and wait.
        shutil.rmtree(path, ignore_errors=True)
        return False
    with open(os.path.join(path, "owner.txt"), "w", encoding="utf-8") as f:
        f.write(f"{owner} pid {os.getpid()} {datetime.datetime.now().isoformat()}\n")
    return True


@contextlib.contextmanager
def held(owner):
    """Waits for and holds the game lock for the duration of the block."""
    waited = False
    while not _try_acquire(owner):
        if not waited:
            print(f"game lock: waiting for {lock_path()} / a running S3.EXE", flush=True)
            waited = True
        time.sleep(POLL_SECONDS)
    try:
        yield
    finally:
        shutil.rmtree(lock_path(), ignore_errors=True)


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "status"
    path = lock_path()
    if command == "status":
        if os.path.isdir(path):
            owner = open(os.path.join(path, "owner.txt"), encoding="utf-8",
                         errors="replace").read().strip() if os.path.exists(
                os.path.join(path, "owner.txt")) else "(no owner.txt)"
            print(f"locked: {owner}")
        else:
            print("not locked")
        print(f"S3.EXE running: {game_running()}")
        return 0
    if command == "release":
        shutil.rmtree(path, ignore_errors=True)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
