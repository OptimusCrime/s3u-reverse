#!/usr/bin/env python3
"""Compare two world dumps (s3re_launcher --trace-determinism --dump-tick N).

Usage: python tools/diff_world_dumps.py A.bin B.bin [--all] [--runs N]

Groups the differing bytes by region: header offsets, map tile fields
(byte offset inside the 24-byte tile record, with the range of tiles), and
offsets after the tile array ("rest", shown as world offsets). By default
the bytes the determinism trace leaves out of its hashes are ignored (same
lists as src/s3re/trace/determinism.cpp); --all shows them too.
"""

import argparse
import sys
from collections import defaultdict

MAP_TILES = 0x67440
TILE = 24
ROW = 0x300 * TILE
REST = MAP_TILES + 0x300 * ROW

# Keep in sync with src/s3re/trace/determinism.cpp.
HEADER_SKIPS = [(0x2d02c, 0x2d244), (0x2d254, 0x2d264), (0x2d27c, 0x2d280), (0x2d284, 0x2d294),
                (0x2d2a0, 0x2d2a4), (0x2d2e4, 0x2d2ec), (0x2d2f0, 0x2d2f4), (0x2d300, 0x2d302)]
REST_SKIPS = [(0x1222ed4, 0x1223656), (0x1539484, 0x15394b0)]
TILE_SKIPS = (15, 17)
TILE_REDRAW = (13, 0x80)  # byte, bit


def skipped(offset):
    if offset < MAP_TILES:
        return any(a <= offset < b for a, b in HEADER_SKIPS)
    if offset < REST:
        field = (offset - MAP_TILES) % ROW % TILE
        return TILE_SKIPS[0] <= field < TILE_SKIPS[1]
    return any(a <= offset < b for a, b in REST_SKIPS)


def differs(a, b, offset, all_bytes):
    if a[offset] == b[offset]:
        return False
    if all_bytes:
        return True
    if skipped(offset):
        return False
    if MAP_TILES <= offset < REST and (offset - MAP_TILES) % ROW % TILE == TILE_REDRAW[0]:
        return (a[offset] ^ b[offset]) & ~TILE_REDRAW[1] & 0xff != 0
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--all", action="store_true", help="include bytes the hashes skip")
    parser.add_argument("--runs", type=int, default=40, help="header/rest runs to list")
    args = parser.parse_args()
    a = open(args.a, "rb").read()
    b = open(args.b, "rb").read()
    if len(a) != len(b):
        print(f"sizes differ: {len(a)} vs {len(b)}; comparing the common part")
    size = min(len(a), len(b))

    diffs = []
    chunk = 1 << 16
    for start in range(0, size, chunk):
        end = min(start + chunk, size)
        if a[start:end] != b[start:end]:
            diffs.extend(i for i in range(start, end) if differs(a, b, i, args.all))
    print(f"{len(diffs)} differing bytes{'' if args.all else ' (outside the skipped fields)'}")

    tiles = defaultdict(list)  # field -> [(x, y, a, b)]
    runs = []                  # header/rest: [start, end]
    for i in diffs:
        if MAP_TILES <= i < REST:
            t = i - MAP_TILES
            tiles[t % ROW % TILE].append((t // ROW, t % ROW // TILE, a[i], b[i]))
        elif runs and i - runs[-1][1] <= 4:
            runs[-1][1] = i
        else:
            runs.append([i, i])

    for field, entries in sorted(tiles.items()):
        xs = [e[0] for e in entries]
        ys = [e[1] for e in entries]
        examples = ", ".join(f"({x},{y}) {va:#x}->{vb:#x}" for x, y, va, vb in entries[:4])
        print(f"tile byte {field:2}: {len(entries):6} tiles, x {min(xs)}..{max(xs)}, "
              f"y {min(ys)}..{max(ys)}; e.g. {examples}")
    for start, end in runs[:args.runs]:
        region = "header" if start < MAP_TILES else "rest  "
        print(f"{region} {start:#09x}..{end:#09x}  A={a[start:end + 1][:24].hex()}  "
              f"B={b[start:end + 1][:24].hex()}")
    if len(runs) > args.runs:
        print(f"... {len(runs) - args.runs} more runs")
    return 0 if not diffs else 1


if __name__ == "__main__":
    sys.exit(main())
