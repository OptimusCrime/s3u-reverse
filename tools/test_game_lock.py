#!/usr/bin/env python3
"""Checks tools/game_lock.py without a game: fake waiters in a temporary
game directory (S3RE_GAME_DIR) must get the lock in arrival order, a ticket
and a lock of dead processes must not block anyone.

Usage: python tools/test_game_lock.py   (about half a minute; exit code 0 = ok)
"""

import os
import subprocess
import sys
import tempfile
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import game_lock  # noqa: E402

WAITER = r"""
import os, sys, time
sys.path.insert(0, sys.argv[1])
import game_lock
name, log = sys.argv[2], sys.argv[3]
with game_lock.held(name, max_wait=120):
    with open(log, "a") as f:
        f.write(f"{name} {time.time():.2f}\n")
    time.sleep(1.5)
"""


def dead_pid():
    process = subprocess.Popen([sys.executable, "-c", "pass"])
    process.wait()
    return process.pid


def main():
    with tempfile.TemporaryDirectory() as directory:
        os.environ["S3RE_GAME_DIR"] = directory
        log = os.path.join(directory, "order.txt")
        # Leftovers of a crashed run: an old ticket and the lock of a dead pid.
        pid = dead_pid()
        os.makedirs(game_lock.queue_path())
        open(os.path.join(game_lock.queue_path(), f"{1:020d}-{pid}-crashed.ticket"), "w").close()
        os.mkdir(game_lock.lock_path())
        with open(os.path.join(game_lock.lock_path(), "owner.txt"), "w") as f:
            f.write(f"crashed pid {pid} 2026-01-01T00:00:00\n")

        waiters = []
        for name in ("first", "second", "third"):
            waiters.append(subprocess.Popen([sys.executable, "-c", WAITER, TOOLS, name, log],
                                            env=dict(os.environ)))
            time.sleep(1.0)  # arrival order
        codes = [w.wait(timeout=180) for w in waiters]
        order = [line.split()[0] for line in open(log)] if os.path.exists(log) else []
        leftovers = os.listdir(game_lock.queue_path()) + (
            ["lock"] if os.path.exists(game_lock.lock_path()) else [])
        ok = codes == [0, 0, 0] and order == ["first", "second", "third"] and not leftovers
        print(f"exit codes {codes}, order {order}, leftovers {leftovers}: {'ok' if ok else 'FAILED'}")

        # A timeout: someone holds the lock, max_wait expires, the ticket goes.
        game_lock.acquire("holder")
        child = subprocess.run([sys.executable, "-c",
                                "import sys; sys.path.insert(0, sys.argv[1]); import game_lock\n"
                                "try:\n game_lock.acquire('late', max_wait=4)\nexcept TimeoutError:\n"
                                " sys.exit(7)", TOOLS], env=dict(os.environ))
        game_lock.release("holder")
        timeout_ok = child.returncode == 7 and not os.listdir(game_lock.queue_path())
        print(f"timeout: exit {child.returncode}, queue {os.listdir(game_lock.queue_path())}: "
              f"{'ok' if timeout_ok else 'FAILED'}")
        return 0 if ok and timeout_ok else 1


if __name__ == "__main__":
    sys.exit(main())
