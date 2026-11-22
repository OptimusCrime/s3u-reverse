#!/usr/bin/env python3
"""Checks tools/command_log.py (the recording formats) without a game.

- every recording in recordings/ parses, and writing it again gives the
  same bytes (the files are in normal form);
- binary (version 1-3) -> version 4 text -> parsed again: header and every
  command byte identical; with --binary DIR also for the .bin recordings in
  DIR (e.g. the main checkout's build/x86/bin/Debug/recordings/), and the
  converted file must equal the one in recordings/ of the same name;
- random recordings round-trip; CRLF line endings and comments are read;
- malformed text is rejected (the same cases as the DLL's
  RecordingFormat_RejectsBadText test).

Usage: python tools/test_command_log.py [--binary DIR]   (exit code 0 = ok)
"""

import argparse
import dataclasses
import glob
import os
import random
import struct
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import command_log as cl  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL", message)


def same(a: cl.Recording, b: cl.Recording) -> bool:
    return (a.seed, a.settings, a.map or "", a.has_setup, a.commands) == \
        (b.seed, b.settings, b.map or "", b.has_setup, b.commands) and \
        (not a.has_setup or a.setup == b.setup)


def binary_v3(recording: cl.Recording) -> bytes:
    """An S3RECMD3 file (as the DLL wrote before version 4)."""
    map_bytes = (recording.map or "").encode("latin-1")
    seed = cl.UNKNOWN_SEED if recording.seed is None else recording.seed
    data = cl.MAGIC_V3 + struct.pack("<IIH", seed, recording.settings, len(map_bytes)) + map_bytes
    data += struct.pack("<H8I", 32, *recording.setup)
    for step, dwords in recording.commands:
        data += struct.pack(f"<II{len(dwords)}I", step, len(dwords), *dwords)
    return data


def random_recording(rng: random.Random, has_setup: bool) -> cl.Recording:
    step = rng.randrange(10)
    commands = []
    for _ in range(rng.randrange(40)):
        step += rng.randrange(3)
        commands.append((step, [rng.getrandbits(32) for _ in range(rng.randint(1, 64))]))
    return cl.Recording(4, rng.randrange(0xFFFF), rng.randrange(2),
                        rng.choice(["", r"Map\MULTI\384-2-Lakes.map", "a b.map"]), commands,
                        tuple(rng.getrandbits(32) for _ in range(8)) if has_setup
                        else cl.SETUP_DEFAULTS, b"", has_setup)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", help="also convert the .bin recordings in this folder")
    args = parser.parse_args()

    committed = sorted(glob.glob(os.path.join(cl.RECORDINGS_DIR, "*" + cl.EXTENSION)))
    check(committed, "no recordings in recordings/")
    for path in committed:
        data = open(path, "rb").read()
        recording = cl.parse(data)
        check(recording.version == 4, f"{path}: not version 4")
        check(cl.format_text(recording).encode(cl.MAP_ENCODING) == data,
              f"{path}: not in normal form (command_log.py convert FILE FILE)")
        check(b"\r" not in data, f"{path}: CR in the file")
        print(f"ok  {os.path.basename(path)}: {len(recording.commands)} commands")

    if args.binary:
        for path in sorted(glob.glob(os.path.join(args.binary, "*.bin"))):
            binary = cl.parse(open(path, "rb").read())
            if binary.version == 1:
                continue
            text = cl.format_text(dataclasses.replace(binary, version=4))
            back = cl.parse(text.encode())
            check(same(binary, back), f"{path}: binary -> text -> parsed differs")
            raw_a = b"".join(struct.pack(f"<II{len(d)}I", s, len(d), *d) for s, d in binary.commands)
            raw_b = b"".join(struct.pack(f"<II{len(d)}I", s, len(d), *d) for s, d in back.commands)
            check(raw_a == raw_b, f"{path}: command bytes differ")
            stem = os.path.splitext(os.path.basename(path))[0]
            target = os.path.join(cl.RECORDINGS_DIR, stem + cl.EXTENSION)
            if os.path.exists(target):
                check(open(target, "rb").read() == text.encode(),
                      f"{target} differs from the conversion of {path}")
            print(f"ok  {path}: {len(binary.commands)} commands round-trip")

    rng = random.Random(4)
    for round_ in range(300):
        original = random_recording(rng, round_ % 2 == 0)
        text = cl.format_text(original)
        back = cl.parse(text.encode())
        check(same(original, back), f"random round {round_}: text round trip differs")
        crlf = cl.parse(text.replace("\n", "\r\n").encode())
        check(same(original, crlf), f"random round {round_}: CRLF version differs")
        if original.has_setup:
            from_binary = cl.parse(binary_v3(original))
            check(from_binary.version == 3 and same(original, from_binary),
                  f"random round {round_}: binary v3 differs")
            check(cl.format_text(from_binary) == text, f"random round {round_}: v3 -> v4 text")

    example = (b"s3re-recording 4\r\n# comment\nseed 1234  # trailing\nsettings 1\n"
               b"map Map\\MULTI\\384-2-Lakes.map\n"
               b"setup flags=1 size=768 symmetry=2 field0c=5 map-seed=1234 goods=3 race=255 "
               b"computers=2\n\n12 21fb0c00 9d2b0000 00a8f612  # cmd fb\n"
               b"12\t21010C00 AABBCCDD 01020304\n4294967295 21030400")
    r = cl.parse(example)
    check(r.seed == 1234 and r.settings == 1 and r.map == r"Map\MULTI\384-2-Lakes.map" and
          r.setup == (1, 768, 2, 5, 1234, 3, 255, 2) and r.commands == [
              (12, [0x000cfb21, 0x00002b9d, 0x12f6a800]),
              (12, [0x000c0121, 0xddccbbaa, 0x04030201]),
              (0xFFFFFFFF, [0x00040321])], "example file parsed wrongly")
    check(cl.command_line(12, [0x000cfb21, 0x00002b9d, 0x12f6a800]) ==
          "12 21fb0c00 9d2b0000 00a8f612  # cmd fb", "command line format")

    head = b"s3re-recording 4\nseed 1\nsettings 0\nmap x.map\n"
    bad = [b"s3re-recording 5\nseed 1\nsettings 0\nmap\n",
           b"s3re-recording 4\nsettings 0\nmap\n",
           b"s3re-recording 4\nseed 1\nmap\n",
           b"s3re-recording 4\nseed 1\nsettings 0\n",
           b"s3re-recording 4\nseed 1\nseed 2\nsettings 0\nmap\n",
           b"s3re-recording 4\nseed -1\nsettings 0\nmap\n",
           b"s3re-recording 4\nseed 4294967296\nsettings 0\nmap\n",
           b"s3re-recording 4\nseed 1\nsettings 0\nmap\ncolour blue\n",
           b"s3re-recording 4\nseed 1\nsettings 0\nmap\nsetup flags=1\n",
           head + b"5 21fb0c0\n", head + b"5 21fb0c000\n", head + b"5 21fb0c0g\n",
           head + b"5\n", head + b"5x 21fb0c00\n", head + b"1 21fb0c00\nseed 2\n",
           head + b"1" + b" 00000000" * 65 + b"\n"]
    for text in bad:
        try:
            cl.parse(text)
            check(False, f"accepted bad text {text!r}")
        except cl.FormatError:
            pass
    check(len(cl.parse(head + b"1 21fb0c00\n").commands) == 1, "minimal file rejected")

    print("command_log tests:", "FAILED" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
