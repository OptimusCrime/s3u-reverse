#!/usr/bin/env python3
"""List or edit a command recording (s3re_launcher --record-commands).

Usage:
  python tools/command_log.py list FILE
  python tools/command_log.py drop FILE OUT INDEX     write OUT without command INDEX

The format is described in src/s3re/replay/command_log.h: "S3RECMD1", then
per command: uint32 slot, uint32 dword count, the message dwords. A command
message starts with bytes 0x21 (type), command id, size in bytes, 0.
`drop` makes a deliberately wrong recording, to check that a replay really
follows the file (the replay must then diverge from the original run).
"""

import struct
import sys

MAGIC = b"S3RECMD1"


def read(path):
    data = open(path, "rb").read()
    if not data.startswith(MAGIC):
        sys.exit(f"{path}: not a command recording")
    commands, offset = [], len(MAGIC)
    while offset < len(data):
        slot, count = struct.unpack_from("<II", data, offset)
        offset += 8
        dwords = list(struct.unpack_from(f"<{count}I", data, offset))
        offset += 4 * count
        commands.append((slot, dwords))
    return commands


def write(path, commands):
    with open(path, "wb") as out:
        out.write(MAGIC)
        for slot, dwords in commands:
            out.write(struct.pack(f"<II{len(dwords)}I", slot, len(dwords), *dwords))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "list":
        for index, (slot, dwords) in enumerate(read(sys.argv[2])):
            raw = b"".join(struct.pack("<I", d) for d in dwords)
            print(f"{index:5} slot {slot:6}  cmd {raw[1]:02x}  {raw.hex(' ', 4)}")
        return 0
    if len(sys.argv) == 5 and sys.argv[1] == "drop":
        commands = read(sys.argv[2])
        index = int(sys.argv[4])
        dropped = commands.pop(index)
        write(sys.argv[3], commands)
        print(f"dropped command {index} (slot {dropped[0]}); {len(commands)} left")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
