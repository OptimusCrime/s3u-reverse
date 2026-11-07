#!/usr/bin/env python3
"""Play the running game with random (seeded) mouse input, for recordings.

Usage: python tools/synthetic_input.py [--seed N] [--duration S] [--start-delay S]
                                       [--log FILE] [--script ACTIONS]

Waits until S3.EXE's window is in the foreground, then for --duration seconds
performs random actions through SendInput (the same path as a real mouse):
open the build menu and place buildings, select and move settlers, change
the goods and settlers menus (the camera stays where the game puts it). Every
action is checked to happen while the game window is in front; otherwise
the script waits, so it never clicks into other programs.

The screen layout assumes the game's 640x480 full-screen mode:
  x < 135          left panel: minimap (top), tabs at y~165, menu below
  x >= 140         the map
  y > 440 (panel)  system buttons (options, quit): never clicked
  top-left icons   never clicked

--script runs a fixed list instead, e.g. "L25,165 S1 L60,250" (L = left
click at x,y, R = right click, S = sleep seconds, P<file> = save a BMP
screenshot), for testing coordinates.

The input is random, not a replayable input stream: what's replayed later
is the recorded commands (tools/check_replay.py).
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import random
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

INPUT_MOUSE = 0
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010
MOUSEEVENTF_ABSOLUTE = 0x8000


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def send_mouse(flags, x=0, y=0):
    event = INPUT(type=INPUT_MOUSE)
    event.mi = MOUSEINPUT(x, y, 0, flags, 0, 0)
    if user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT)) != 1:
        raise OSError(ctypes.get_last_error(), "SendInput failed")


def game_in_front():
    """True if the foreground window belongs to a process named S3.EXE."""
    window = user32.GetForegroundWindow()
    if not window:
        return False
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
    process = kernel32.OpenProcess(0x1000, False, pid.value)  # QUERY_LIMITED_INFORMATION
    if not process:
        return False
    try:
        name = ctypes.create_unicode_buffer(1024)
        size = wt.DWORD(len(name))
        if not kernel32.QueryFullProcessImageNameW(process, 0, name, ctypes.byref(size)):
            return False
        return os.path.basename(name.value).lower() == "s3.exe"
    finally:
        kernel32.CloseHandle(process)


def wait_for_game(timeout):
    end = time.time() + timeout
    while time.time() < end:
        if game_in_front():
            return True
        time.sleep(0.2)
    return False


def move(x, y):
    width = user32.GetSystemMetrics(0)
    height = user32.GetSystemMetrics(1)
    send_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE,
               (x * 65535) // max(width - 1, 1), (y * 65535) // max(height - 1, 1))


def click(x, y, button="L"):
    if not game_in_front():
        return False
    down, up = ((MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP) if button == "L"
                else (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP))
    move(x, y)
    time.sleep(0.08)
    send_mouse(down)
    time.sleep(0.06)
    send_mouse(up)
    time.sleep(0.12)
    return True


def drag(x0, y0, x1, y1):
    """Left-button drag (box selection of settlers/soldiers)."""
    if not game_in_front():
        return False
    move(x0, y0)
    time.sleep(0.08)
    send_mouse(MOUSEEVENTF_LEFTDOWN)
    for step in range(1, 9):
        time.sleep(0.03)
        move(x0 + (x1 - x0) * step // 8, y0 + (y1 - y0) * step // 8)
    time.sleep(0.08)
    send_mouse(MOUSEEVENTF_LEFTUP)
    time.sleep(0.12)
    return True


# Screen regions (640x480 full screen).
MAP = (145, 10, 630, 470)
MENU = (15, 195, 125, 425)
MINIMAP = (55, 30, 125, 125)
TAB_BUILD, TAB_GOODS, TAB_SETTLERS = (25, 165), (68, 165), (108, 165)
BUILD_CATEGORIES = (22, 52, 82, 112)  # x of the category buttons at y=200
BUILDINGS = (20, 222, 115, 420)        # building icons below the categories


def point(rng, area):
    x0, y0, x1, y1 = area
    return rng.randint(x0, x1), rng.randint(y0, y1)


def random_action(rng):
    """Returns a list of (description, button, x, y) clicks."""
    # No minimap clicks: they move the camera away from the player's land,
    # after which no building site is ever visible.
    kind = rng.choices(["build", "build", "build", "move", "goods", "settlers",
                        "map"], k=1)[0]
    if kind == "build":
        return [("build tab", "L", *TAB_BUILD),
                ("category", "L", rng.choice(BUILD_CATEGORIES), 200),
                ("building", "L", *point(rng, BUILDINGS)),
                ("place", "SITE", 0, 0)]
    if kind == "move":
        x, y = point(rng, (145, 10, 530, 380))
        return [("box select", "DRAG", x, y, x + rng.randint(40, 100), y + rng.randint(40, 90)),
                ("move to", "R", *point(rng, MAP))]
    if kind == "goods":
        return [("goods tab", "L", *TAB_GOODS), ("goods menu", "L", *point(rng, MENU)),
                ("goods menu", "L", *point(rng, MENU))]
    if kind == "settlers":
        return [("settlers tab", "L", *TAB_SETTLERS),
                ("settlers menu", "L", *point(rng, MENU))]
    return [("map", rng.choice("LR"), *point(rng, MAP))]


def place_building(rng, log):
    """In placing mode: click a random building-site marker. If the chosen
    menu slot showed no sites (empty slot, or nowhere to build it), try
    other buildings of the open category."""
    for attempt in range(4):
        time.sleep(0.3)
        sites = find_site_markers(MAP)
        if sites:
            x, y = rng.choice(sites)
            ok = click(x, y, "L")
            log_line(log, f"place: L {x},{y}{'' if ok else ' (skipped)'}")
            return
        x, y = point(rng, BUILDINGS)
        log_line(log, f"no building site visible; other building: L {x},{y}")
        click(x, y, "L")
    click(*point(rng, MAP), "R")  # leave placing mode


def log_line(log, text):
    line = f"{time.strftime('%H:%M:%S')} {text}"
    print(line, flush=True)
    if log:
        log.write(line + "\n")
        log.flush()


def capture():
    """Returns (width, height, stride, bottom-up BGR bytes) of the screen."""
    gdi32 = ctypes.WinDLL("gdi32")
    width, height = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    screen = user32.GetDC(None)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    gdi32.SelectObject(memory, bitmap)
    gdi32.BitBlt(memory, 0, 0, width, height, screen, 0, 0, 0x00CC0020)  # SRCCOPY
    stride = (width * 3 + 3) & ~3
    header = (ctypes.c_uint32 * 10)(40, width, height, 1 | (24 << 16), 0, stride * height,
                                    0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(stride * height)
    gdi32.GetDIBits(memory, bitmap, 0, height, pixels, header, 0)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory)
    user32.ReleaseDC(None, screen)
    return width, height, stride, pixels.raw


def screenshot(path):
    """Saves the screen as a 24-bit BMP."""
    width, height, stride, pixels = capture()
    header = (ctypes.c_uint32 * 10)(40, width, height, 1 | (24 << 16), 0, stride * height,
                                    0, 0, 0, 0)
    with open(path, "wb") as out:
        size = 14 + 40 + len(pixels)
        out.write(b"BM" + size.to_bytes(4, "little") + bytes(4) + (54).to_bytes(4, "little"))
        out.write(bytes(header)[:40])
        out.write(pixels)


def find_site_markers(area):
    """Centres of the 4x4 building-site markers on screen (bright 4x4 blocks
    framed by black pixels), shown while a building is chosen for placing."""
    width, height, stride, pixels = capture()

    def rgb_sum(x, y):
        i = (height - 1 - y) * stride + x * 3
        return pixels[i] + pixels[i + 1] + pixels[i + 2]

    x0, y0, x1, y1 = area
    found = []
    for y in range(max(y0, 1), min(y1, height - 5)):
        for x in range(max(x0, 1), min(x1, width - 5)):
            if (rgb_sum(x, y) > 300 and rgb_sum(x - 1, y) == 0 and rgb_sum(x, y - 1) == 0
                    and rgb_sum(x + 4, y) == 0 and rgb_sum(x, y + 4) == 0
                    and rgb_sum(x + 3, y + 3) > 150):
                found.append((x + 2, y + 2))
    return found


def run_script(script, log):
    for token in script.split():
        if token[0] == "S":
            time.sleep(float(token[1:]))
            continue
        if token[0] == "P":
            screenshot(token[1:])
            log_line(log, f"screenshot {token[1:]}")
            continue
        x, y = (int(v) for v in token[1:].split(","))
        log_line(log, f"{token[0]} {x},{y} {'ok' if click(x, y, token[0]) else 'skipped'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--start-delay", type=float, default=2,
                        help="seconds to wait after the game window appears")
    parser.add_argument("--wait", type=float, default=120,
                        help="give up if the game window doesn't appear in time")
    parser.add_argument("--log")
    parser.add_argument("--script")
    args = parser.parse_args()

    log = open(args.log, "w", encoding="utf-8") if args.log else None
    if not wait_for_game(args.wait):
        log_line(log, "game window did not appear")
        return 2
    time.sleep(args.start_delay)

    if args.script:
        run_script(args.script, log)
        return 0

    rng = random.Random(args.seed)
    end = time.time() + args.duration
    actions = 0
    while time.time() < end:
        if not game_in_front():
            time.sleep(0.5)
            if not wait_for_game(10):
                log_line(log, "game window gone; stopping")
                break
            continue
        for description, button, x, y, *rest in random_action(rng):
            if button == "SITE":
                place_building(rng, log)
                continue
            if button == "DRAG":
                ok = drag(x, y, *rest)
            else:
                ok = click(x, y, button)
            log_line(log, f"{description}: {button} {x},{y} {' '.join(map(str, rest))}"
                          f"{'' if ok else ' (skipped)'}")
            time.sleep(rng.uniform(0.2, 0.6))
        actions += 1
        time.sleep(rng.uniform(0.5, 2.0))
    log_line(log, f"done: {actions} actions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
