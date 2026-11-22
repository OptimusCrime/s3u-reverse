#!/usr/bin/env python3
"""List, convert, edit or upgrade a command recording (s3re_launcher --record-commands).

Usage:
  python tools/command_log.py list FILE
  python tools/command_log.py convert IN OUT        write IN (any version) as version 4
                                                    text (also normalises a v4 file)
  python tools/command_log.py drop FILE OUT INDEX   write OUT without command INDEX
  python tools/command_log.py upgrade FILE OUT --seed N --map PATH [--no-amazons]
                                                    write a version 1 recording as
                                                    version 4 (with seed and map)

FILE may be a path or the name of a file in recordings/ (with or without
.s3rec). The formats are described in src/common/recording_format.h.
Version 4 (written since 2026-11-22, extension .s3rec, committed in
recordings/) is a text file:

  s3re-recording 4
  seed 1234
  settings 1
  map Map\\MULTI\\384-2-Lakes.map
  setup flags=1 size=768 symmetry=0 field0c=0 map-seed=1234 goods=3 race=255 computers=2
  12 21fb0c00 9d2b0000 00a8f612  # cmd fb

(step = number of 0x20 step messages before the command, then the message
bytes in memory order, 4 per group; '#' starts a comment). Versions 1-3 are
binary ("S3RECMD1/2/3"): version 2 has seed, settings and map, version 3
adds the game setup of a --random-map game, version 1 has the commands
only. Readers tell them apart by the first bytes.
`drop` makes a deliberately wrong recording, to check that a replay really
follows the file (the replay must then diverge from the original run).
`upgrade` is for recordings made before version 2: give the seed and map
they were recorded with (the settings default to Amazons included, the
game's setting on the development machine; check "game settings" in
s3re.log).
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import struct
import sys
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECORDINGS_DIR = os.path.join(ROOT, "recordings")
EXTENSION = ".s3rec"

MAGIC_V1 = b"S3RECMD1"
MAGIC_V2 = b"S3RECMD2"
MAGIC_V3 = b"S3RECMD3"
TEXT_MAGIC = b"s3re-recording 4"
SETUP_FIELDS = ("flags", "size", "symmetry", "field0c", "map_seed", "goods", "race",
                "computers")
SETUP_KEYS = tuple(name.replace("_", "-") for name in SETUP_FIELDS)  # in the text format
SETUP_DEFAULTS = (0, 0, 0, 0, 0, 0, 0xFF, 0)
SETUP_RANDOM_MAP = 1
GOODS_NAMES = ("the map's", "low", "medium", "high")
RACE_NAMES = ("roman", "egyptian", "asian", "amazon")
SETTING_AMAZONS = 1
UNKNOWN_SEED = 0xFFFFFFFF
MAX_DWORDS = 64
# The map line holds the -map argument's bytes (ANSI code page); everything
# else in a version 4 file is ASCII.
MAP_ENCODING = "mbcs" if os.name == "nt" else "latin-1"
HEADER_COMMENT = ("# The Settlers 3 command recording (s3re). "
                  "Format: src/common/recording_format.h")
COMMANDS_COMMENT = "# step  message bytes (hex, memory order, 4 bytes per group)"


class FormatError(ValueError):
    pass


@dataclass
class Recording:
    version: int
    seed: int | None = None       # None: unknown (version 1, or 0xFFFFFFFF)
    settings: int | None = None
    map: str | None = None
    # (step, dwords): step = number of 0x20 messages before the command.
    commands: list[tuple[int, list[int]]] = field(default_factory=list)
    # The game setup (SETUP_FIELDS); has_setup: known (version 3, version 4
    # with a setup line). Without it a replay doesn't check the setup.
    setup: tuple[int, ...] = SETUP_DEFAULTS
    setup_extra: bytes = b""  # version 3: bytes after the known fields
    has_setup: bool = False

    def setup_dict(self) -> dict:
        return dict(zip(SETUP_FIELDS, self.setup))


def resolve(name: str) -> str:
    """A path, or the name of a recording in recordings/ (extension optional)."""
    if os.path.exists(name):
        return name
    for candidate in (os.path.join(RECORDINGS_DIR, name),
                      os.path.join(RECORDINGS_DIR, name + EXTENSION)):
        if os.path.exists(candidate):
            return candidate
    return name


def _decimal(text: str, what: str, line: int) -> int:
    if not text.isdigit() or not text.isascii() or len(text) > 10 or int(text) > 0xFFFFFFFF:
        raise FormatError(f"line {line}: bad {what} {text!r}")
    return int(text)


def parse_text(data: bytes) -> Recording:
    """Parses a version 4 recording (the same rules as recording_format.h)."""
    recording = Recording(4)
    seen = set()
    for number, raw in enumerate(data.split(b"\n"), 1):
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        if number == 1:
            if raw != TEXT_MAGIC:
                raise FormatError('line 1: not "s3re-recording 4"')
            continue
        line = raw.split(b"#", 1)[0].strip(b" \t")
        if not line:
            continue
        if line[:1].isdigit():
            words = re.split(rb"[ \t]+", line)
            step = _decimal(words[0].decode("latin-1"), "step", number)
            if not 1 <= len(words) - 1 <= MAX_DWORDS:
                raise FormatError(f"line {number}: a command needs 1..{MAX_DWORDS} groups of "
                                  "8 hex digits")
            dwords = []
            for word in words[1:]:
                try:
                    if len(word) != 8:
                        raise ValueError
                    dwords.append(struct.unpack("<I", bytes.fromhex(word.decode("ascii")))[0])
                except ValueError:
                    raise FormatError(f"line {number}: bad hex group {word!r} "
                                      "(8 hex digits expected)") from None
            recording.commands.append((step, dwords))
            continue
        if recording.commands:
            raise FormatError(f"line {number}: header line after the first command")
        key, value = re.match(rb"([^ \t]+)[ \t]*(.*)", line).groups()
        key = key.decode("latin-1")
        if key in seen:
            raise FormatError(f"line {number}: {key!r} given twice")
        seen.add(key)
        if key == "seed":
            seed = _decimal(value.decode("latin-1"), "seed", number)
            recording.seed = None if seed == UNKNOWN_SEED else seed
        elif key == "settings":
            recording.settings = _decimal(value.decode("latin-1"), "settings", number)
        elif key == "map":
            recording.map = value.decode(MAP_ENCODING, "replace")
        elif key == "setup":
            values = {}
            for word in value.decode("latin-1").split():
                name, equals, number_text = word.partition("=")
                if not equals or name not in SETUP_KEYS or name in values:
                    raise FormatError(f"line {number}: bad or repeated setup field {word!r}")
                values[name] = _decimal(number_text, name, number)
            missing = [k for k in SETUP_KEYS if k not in values]
            if missing:
                raise FormatError(f"line {number}: setup lacks {missing[0]!r}")
            recording.setup = tuple(values[k] for k in SETUP_KEYS)
            recording.has_setup = True
        else:
            raise FormatError(f"line {number}: unknown line {key!r}")
    if not {"seed", "settings", "map"} <= seen:
        raise FormatError("the header needs 'seed', 'settings' and 'map'")
    return recording


def parse_binary(data: bytes) -> Recording:
    if data.startswith(MAGIC_V2) or data.startswith(MAGIC_V3):
        version = 3 if data.startswith(MAGIC_V3) else 2
        seed, settings, map_length = struct.unpack_from("<IIH", data, 8)
        offset = 8 + 10
        recording = Recording(version, None if seed == UNKNOWN_SEED else seed, settings,
                              data[offset:offset + map_length].decode(MAP_ENCODING, "replace"))
        offset += map_length
        if version == 3:
            (setup_length,) = struct.unpack_from("<H", data, offset)
            offset += 2
            block = data[offset:offset + setup_length]
            offset += setup_length
            known = min(len(block) // 4, len(SETUP_FIELDS))
            recording.setup = (struct.unpack_from(f"<{known}I", block) +
                               SETUP_DEFAULTS[known:])
            recording.setup_extra = block[known * 4:]
            recording.has_setup = True
    elif data.startswith(MAGIC_V1):
        recording, offset = Recording(1), len(MAGIC_V1)
    else:
        raise FormatError("not a command recording")
    try:
        while offset < len(data):
            slot, count = struct.unpack_from("<II", data, offset)
            offset += 8
            if not 1 <= count <= MAX_DWORDS:
                raise FormatError(f"bad dword count {count} at byte {offset - 4}")
            dwords = list(struct.unpack_from(f"<{count}I", data, offset))
            offset += 4 * count
            recording.commands.append((slot, dwords))
    except struct.error:
        raise FormatError("truncated command record") from None
    return recording


def parse(data: bytes) -> Recording:
    return parse_text(data) if data.startswith(TEXT_MAGIC) else parse_binary(data)


def read(path) -> Recording:
    path = resolve(str(path))
    try:
        return parse(open(path, "rb").read())
    except FormatError as error:
        sys.exit(f"{path}: {error}")


def command_line(step: int, dwords: list[int]) -> str:
    raw = struct.pack(f"<{len(dwords)}I", *dwords)
    return f"{step} {raw.hex(' ', 4)}  # cmd {raw[1]:02x}"


def format_text(recording: Recording) -> str:
    """Version 4 text (byte for byte what the DLL's recorder writes)."""
    if recording.version == 1 and recording.seed is None:
        raise FormatError("a version 1 recording has no seed or map: use `upgrade`")
    if recording.setup_extra:
        raise FormatError("the setup has fields this tool doesn't know")
    seed = UNKNOWN_SEED if recording.seed is None else recording.seed
    map_text = recording.map or ""
    if "#" in map_text or "\n" in map_text or map_text != map_text.strip(" \t"):
        raise FormatError(f"map {map_text!r} can't be written in version 4")
    lines = ["s3re-recording 4", HEADER_COMMENT, f"seed {seed}",
             f"settings {recording.settings or 0}",
             f"map {map_text}" if map_text else "map"]
    if recording.has_setup:
        lines.append("setup " + " ".join(f"{k}={v}" for k, v in zip(SETUP_KEYS, recording.setup)))
    lines.append(COMMANDS_COMMENT)
    lines += [command_line(step, dwords) for step, dwords in recording.commands]
    return "\n".join(lines) + "\n"


def write(path, recording: Recording):
    """Writes version 4 text, except a version 1 recording without seed,
    which stays binary version 1."""
    if recording.version == 1 and recording.seed is None:
        with open(path, "wb") as out:
            out.write(MAGIC_V1)
            for slot, dwords in recording.commands:
                out.write(struct.pack(f"<II{len(dwords)}I", slot, len(dwords), *dwords))
        return
    data = format_text(recording).encode(MAP_ENCODING)
    with open(path, "wb") as out:
        out.write(data)


def describe(recording: Recording) -> str:
    if recording.version == 1:
        return "version 1 (no seed or map: replay needs --seed and -map)"
    amazons = "included" if (recording.settings or 0) & SETTING_AMAZONS else "not included"
    seed = "unknown" if recording.seed is None else recording.seed
    text = (f"version {recording.version}, seed {seed}, map '{recording.map}', "
            f"random races: Amazons {amazons}")
    setup = recording.setup_dict()
    if recording.has_setup and setup["flags"] & SETUP_RANDOM_MAP:
        goods = setup["goods"]
        race = setup["race"]
        computers = setup["computers"]
        text += (f"; random map {setup['size']},{setup['symmetry']},{setup['field0c']} "
                 f"(size, symmetry, field0c), map seed {setup['map_seed']}, start goods "
                 f"{GOODS_NAMES[goods] if goods < 4 else goods}, your race "
                 f"{RACE_NAMES[race] if race < 4 else 'random'}, "
                 + (f"{computers} computer opponent(s)" if computers
                    else "players from the map file"))
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list")
    p.add_argument("file")
    p = sub.add_parser("convert")
    p.add_argument("file")
    p.add_argument("out")
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
        for index, (step, dwords) in enumerate(recording.commands):
            raw = struct.pack(f"<{len(dwords)}I", *dwords)
            print(f"{index:5} step {step:6}  cmd {raw[1]:02x}  {raw.hex(' ', 4)}")
    elif args.command == "convert":
        if recording.version == 1:
            sys.exit(f"{args.file} is version 1 (no seed or map): use `upgrade` instead")
        try:
            write(args.out, dataclasses.replace(recording, version=4))
        except FormatError as error:
            sys.exit(f"{args.file}: {error}")
        back = read(args.out)
        if (back.commands != recording.commands or back.seed != recording.seed or
                back.settings != recording.settings or back.map != recording.map or
                back.has_setup != recording.has_setup or
                (back.has_setup and back.setup != recording.setup)):
            sys.exit(f"{args.out}: reading it back gave a different recording (bug)")
        print(f"{args.out}: {describe(back)}, {len(back.commands)} commands")
    elif args.command == "drop":
        dropped = recording.commands.pop(args.index)
        write(args.out, recording)
        print(f"dropped command {args.index} (step {dropped[0]}); "
              f"{len(recording.commands)} left")
    elif args.command == "upgrade":
        if recording.version != 1:
            sys.exit(f"{args.file} is already version {recording.version}")
        if not 0 <= args.seed <= 0xFFFF:
            sys.exit("--seed must be 0..65535")
        recording = dataclasses.replace(recording, version=4, seed=args.seed, map=args.map,
                                        settings=0 if args.no_amazons else SETTING_AMAZONS)
        write(args.out, recording)
        print(f"{args.out}: {describe(recording)}, {len(recording.commands)} commands")
    return 0


if __name__ == "__main__":
    sys.exit(main())
