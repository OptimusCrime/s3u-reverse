#!/usr/bin/env python3
"""A fair (first come, first served) lock for runs that start S3.EXE with a window.

Several agents (or worktrees) may launch the game; two games at once fight
over the screen, DirectDraw and the input. Every tool that starts a windowed
game takes this lock first:

    with game_lock.held("check_save"):
        subprocess.run([launcher, ...])

(or acquire(owner) / release(owner) around the run).

The lock is the directory <game dir>\\s3re-game.lock, created atomically
with mkdir, with owner.txt inside (owner, pid, time). Waiters queue in
<game dir>\\s3re-game.queue: each one writes a ticket file named
<time in ns>-<pid>-<owner>.ticket, and only the oldest ticket may take the
lock, so runs get the game in the order they asked for it. Waiters check
every POLL_SECONDS; tickets of dead processes are removed by any waiter.
A lock is taken only when no S3.EXE game runs (an S3.EXE started by
`s3re_launcher --test`, recognised by its parent's command line, doesn't
count; one whose parent is gone does). A lock whose owner pid is dead, or
without owner.txt (made by hand with mkdir) and older than 2 hours, is
stale when no game runs and is removed. Lock and ticket are removed when
the run ends, also on errors and Ctrl+C (and at exit, as a fallback).

The game directory is S3RE_GAME_DIR, or Settlers3Ultimate in the repository
(in a git worktree that is a junction to the main checkout's, so all
worktrees share one lock). `--test` runs don't open a window and don't need
the lock.

Usage as a script: python tools/game_lock.py status | release
"""

import atexit
import contextlib
import ctypes
import datetime
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STALE_SECONDS = 2 * 3600
POLL_SECONDS = 3


def game_dir():
    return os.environ.get("S3RE_GAME_DIR") or os.path.join(ROOT, "Settlers3Ultimate")


def lock_path():
    return os.path.join(game_dir(), "s3re-game.lock")


def queue_path():
    return os.path.join(game_dir(), "s3re-game.queue")


# -- processes -----------------------------------------------------------------

# Every S3.EXE with its parent's command line, tab-separated, one per line.
_LIST_GAMES = (
    "Get-CimInstance Win32_Process -Filter \"Name='S3.EXE'\" | ForEach-Object { "
    "$p = Get-CimInstance Win32_Process -Filter \"ProcessId=$($_.ParentProcessId)\"; "
    "\"$($_.ProcessId)`t$($p.CommandLine)\" }")


def is_test_run(parent_command_line):
    """True if the parent is s3re_launcher started with --test (before a
    bare `--`, after which the game's own arguments follow)."""
    if not parent_command_line or "s3re_launcher" not in parent_command_line.lower():
        return False
    for token in parent_command_line.split():
        token = token.strip('"')
        if token == "--":
            return False
        if token == "--test":
            return True
    return False


def running_games():
    """PIDs of the S3.EXE processes that count as a running game: all except
    those started by `s3re_launcher --test` (in-process tests, no window).
    An S3.EXE whose parent is gone counts as a game."""
    result = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", _LIST_GAMES],
                            capture_output=True, text=True)
    if result.returncode != 0:
        # Fall back to "any S3.EXE is a game".
        listing = subprocess.run(["tasklist", "/FI", "IMAGENAME eq S3.EXE", "/NH"],
                                 capture_output=True, text=True).stdout.lower()
        return ["?"] if "s3.exe" in listing else []
    games = []
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        pid, _, parent = line.partition("\t")
        if not is_test_run(parent):
            games.append(pid.strip())
    return games


def game_running():
    """True if an S3.EXE runs that is not an in-process test run."""
    return bool(running_games())


def pid_alive(pid):
    """True if a process with this id exists (Windows; elsewhere kill 0)."""
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True
    kernel32 = ctypes.windll.kernel32
    kernel32.OpenProcess.restype = ctypes.c_void_p
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return ctypes.GetLastError() == 5  # access denied: it exists
    try:
        code = ctypes.c_ulong()
        kernel32.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))
        return code.value == 259  # STILL_ACTIVE
    finally:
        kernel32.CloseHandle(ctypes.c_void_p(handle))


# -- the lock ------------------------------------------------------------------

def _owner_info():
    """(text of owner.txt or None, owner pid or None) of the current lock."""
    try:
        text = open(os.path.join(lock_path(), "owner.txt"), encoding="utf-8",
                    errors="replace").read().strip()
    except OSError:
        return None, None
    match = re.search(r" pid (\d+) ", text + " ")
    return text, int(match.group(1)) if match else None


def _remove_stale_lock():
    """Removes the lock if its owner is gone (dead pid, or no owner.txt and
    older than STALE_SECONDS) and no game runs."""
    path = lock_path()
    try:
        age = time.time() - os.path.getmtime(path)
    except OSError:
        return
    text, pid = _owner_info()
    if pid is not None:
        stale = not pid_alive(pid)
        why = f"owner pid {pid} is gone"
    else:
        # Made by hand (or owner.txt not written yet: give it a moment).
        stale = age > STALE_SECONDS
        why = f"{age / 3600:.1f} h old, no owner"
    if stale and not game_running():
        print(f"game lock: removing stale lock ({why})", flush=True)
        shutil.rmtree(path, ignore_errors=True)


def _try_lock(owner):
    path = lock_path()
    try:
        os.mkdir(path)
    except FileExistsError:
        _remove_stale_lock()
        return False
    if game_running():
        # Someone runs the game without the lock; give it back and wait.
        shutil.rmtree(path, ignore_errors=True)
        return False
    with open(os.path.join(path, "owner.txt"), "w", encoding="utf-8") as f:
        f.write(f"{owner} pid {os.getpid()} {datetime.datetime.now().isoformat()}\n")
    return True


# -- the queue -----------------------------------------------------------------

_TICKET = re.compile(r"^(\d+)-(\d+)-(.*)\.ticket$")


def _tickets():
    """Live tickets, oldest first: [(name, pid, owner)]. Removes the tickets
    of dead processes."""
    try:
        names = os.listdir(queue_path())
    except OSError:
        return []
    live = []
    for name in sorted(names):
        match = _TICKET.match(name)
        if not match:
            continue
        pid = int(match.group(2))
        if not pid_alive(pid):
            with contextlib.suppress(OSError):
                os.remove(os.path.join(queue_path(), name))
            continue
        live.append((name, pid, match.group(3)))
    return live


def _new_ticket(owner):
    os.makedirs(queue_path(), exist_ok=True)
    safe = re.sub(r"[^A-Za-z0-9_.+]", "_", owner)[:40]
    name = f"{time.time_ns():020d}-{os.getpid()}-{safe}.ticket"
    with open(os.path.join(queue_path(), name), "w", encoding="utf-8") as f:
        f.write(f"{owner} pid {os.getpid()} {datetime.datetime.now().isoformat()}\n")
    return name


def _remove_ticket(name):
    with contextlib.suppress(OSError):
        os.remove(os.path.join(queue_path(), name))


_held_by_us = set()  # owners whose lock this process holds (for release / atexit)


def acquire(owner, max_wait=None):
    """Waits (in arrival order) for the game lock and takes it. Raises
    TimeoutError after max_wait seconds."""
    ticket = _new_ticket(owner)
    atexit.register(_remove_ticket, ticket)
    started = time.time()
    shown = None
    try:
        while True:
            tickets = _tickets()
            names = [t[0] for t in tickets]
            if ticket not in names:  # removed by someone (e.g. `release`): queue again
                ticket = _new_ticket(owner)
                atexit.register(_remove_ticket, ticket)
                continue
            position = names.index(ticket)
            # First in line and the lock is free: take it (unless a game runs
            # without the lock; then wait for it to end).
            if position == 0 and _try_lock(owner):
                _held_by_us.add(owner)
                atexit.register(_release_at_exit, owner)
                return
            state = (position, _owner_info()[0])
            if state != shown:
                holder = state[1] or ("(no owner.txt)" if os.path.isdir(lock_path()) else "nobody")
                print(f"game lock: waiting, {position} ahead in the queue; lock held by {holder}",
                      flush=True)
                shown = state
            if max_wait is not None and time.time() - started > max_wait:
                raise TimeoutError(f"game lock: not acquired within {max_wait} s")
            time.sleep(POLL_SECONDS)
    finally:
        _remove_ticket(ticket)


def release(owner=None):
    """Releases the lock taken by acquire()."""
    _held_by_us.discard(owner)
    shutil.rmtree(lock_path(), ignore_errors=True)


def _release_at_exit(owner):
    if owner in _held_by_us:
        release(owner)


@contextlib.contextmanager
def held(owner, max_wait=None):
    """Waits for and holds the game lock for the duration of the block."""
    acquire(owner, max_wait)
    try:
        yield
    finally:
        release(owner)


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "status"
    path = lock_path()
    if command == "status":
        if os.path.isdir(path):
            text, pid = _owner_info()
            age = time.time() - os.path.getmtime(path)
            alive = "" if pid is None else (" (alive)" if pid_alive(pid) else " (pid gone: stale)")
            print(f"locked {age / 60:.0f} min: {text or '(no owner.txt)'}{alive}")
        else:
            print("not locked")
        tickets = _tickets()
        print(f"queue: {len(tickets)} waiting")
        for i, (name, pid, owner) in enumerate(tickets):
            since = (time.time_ns() - int(name.split('-')[0])) / 1e9
            print(f"  {i + 1}. {owner} pid {pid}, waiting {since / 60:.0f} min")
        print(f"S3.EXE running (not counting --test runs): {game_running()}")
        return 0
    if command == "release":
        shutil.rmtree(path, ignore_errors=True)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
