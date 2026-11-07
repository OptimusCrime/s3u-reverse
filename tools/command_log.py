#!/usr/bin/env python3
"""List, edit or upgrade a command recording (s3re_launcher --record-commands).

Usage:
  python tools/command_log.py list FILE
  python tools/command_log.py drop FILE OUT INDEX     write OUT without command INDEX
  python tools/command_log.py upgrade FILE OUT --seed N --map PATH [--no-amazons]
                                                      write a version 1 recording as
                                                      version 2 (with seed and map)

The format is described in src/common/recording_format.h. Version 2
("S3RECMD2") starts with the game seed, the settings (bit 0: random races
include the Amazons) and the -map argument; version 1 ("S3RECMD1") has
none of that. Then, per command: uint32 slot, uint32 dword count, the
message dwords. A command message starts with bytes 0x21 (type), command
id, size in bytes, 0.
`drop` makes a deliberately wrong recording, to check that a replay really
follows the file (the replay must then diverge from the original run).
`upgrade` is for recordings made before version 2: give the seed and map
they were recorded with (the settings default to Amazons included, the
game's setting on the development machine; check "game settings" in
s3re.log).
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field

MAGIC_V1 = b"S3RECMD1"
MAGIC_V2 = b"S3RECMD2"
SETTING_AMAZONS = 1
UNKNOWN_SEED = 0xFFFFFFFF


@dataclass
class Recording:
    version: int
    seed: int | None = None       # None: unknown (version 1)
    settings: int | None = None
    map: str | None = None
    commands: list[tuple[int, list[int]]] = field(default_factory=list)


def read(path) -> Recording:
    data = open(path, "rb").read()
    if data.startswith(MAGIC_V2):
        seed, settings, map_length = struct.unpack_from("<IIH", data, 8)
        offset = 8 + 10
        recording = Recording(2, None if seed == UNKNOWN_SEED else seed, settings,
                              data[offset:offset + map_length].decode("mbcs", "replace"))
        offset += map_length
    elif data.startswith(MAGIC_V1):
        recording, offset = Recording(1), len(MAGIC_V1)
    else:
        sys.exit(f"{path}: not a command recording")
    while offset < len(data):
        slot, count = struct.unpack_from("<II", data, offset)
        offset += 8
        dwords = list(struct.unpack_from(f"<{count}I", data, offset))
        offset += 4 * count
        recording.commands.append((slot, dwords))
    return recording


def write(path, recording: Recording):
    """Writes in the recording's version (2 needs seed, settings and map)."""
    with open(path, "wb") as out:
        if recording.version == 2:
            map_bytes = (recording.map or "").encode("mbcs")
            seed = UNKNOWN_SEED if recording.seed is None else recording.seed
            out.write(MAGIC_V2 + struct.pack("<IIH", seed, recording.settings or 0,
                                             len(map_bytes)) + map_bytes)
        else:
            out.write(MAGIC_V1)
        for slot, dwords in recording.commands:
            out.write(struct.pack(f"<II{len(dwords)}I", slot, len(dwords), *dwords))


def describe(recording: Recording) -> str:
    if recording.version == 1:
        return "version 1 (no seed or map: replay needs --seed and -map)"
    amazons = "included" if (recording.settings or 0) & SETTING_AMAZONS else "not included"
    seed = "unknown" if recording.seed is None else recording.seed
    return (f"version 2, seed {seed}, map '{recording.map}', "
            f"random races: Amazons {amazons}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list")
    p.add_argument("file")
    p = sub.add_parser("drop")
    p.add_argument("file")
    p.add_argument("out")
    p.add_argument("index", type=int)
    p = sub.add_parser("upgrade")
    p.add_argument("file")
    p.add_argument("out")
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--map", required=True)
    p.add_argument("--no-amazons", action="store_true")
    args = parser.parse_args()

    recording = read(args.file)
    if args.command == "list":
        print(describe(recording))
        for index, (slot, dwords) in enumerate(recording.commands):
            raw = b"".join(struct.pack("<I", d) for d in dwords)
            print(f"{index:5} slot {slot:6}  cmd {raw[1]:02x}  {raw.hex(' ', 4)}")
    elif args.command == "drop":
        dropped = recording.commands.pop(args.index)
        write(args.out, recording)
        print(f"dropped command {args.index} (slot {dropped[0]}); "
              f"{len(recording.commands)} left")
    elif args.command == "upgrade":
        if recording.version != 1:
            sys.exit(f"{args.file} is already version {recording.version}")
        if not 0 <= args.seed <= 0xFFFF:
            sys.exit("--seed must be 0..65535")
        recording.version = 2
        recording.seed = args.seed
        recording.map = args.map
        recording.settings = 0 if args.no_amazons else SETTING_AMAZONS
        write(args.out, recording)
        print(f"{args.out}: {describe(recording)}, {len(recording.commands)} commands")
    return 0


if __name__ == "__main__":
    sys.exit(main())
