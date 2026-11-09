#!/usr/bin/env python3
"""Lint: no raw world offsets in our source (typed access, CLAUDE.md).

World fields are accessed through the structs in src/s3re/game/layout.h
(world->tiles[x][y].owner, world->settlers[n].job, ...); other objects
through small packed structs. This check flags patterns that bring raw
offsets back:

  - an `At<T>(...)` / `WorldField<T>(...)` helper or `world_offset::` use
    (the old per-file raw accessors);
  - a byte-pointer cast of a world pointer
    (`reinterpret_cast<std::uint8_t*>(world)` and the like);
  - a constant whose value lies inside the world object's offset range
    (0x2d000..0x153a128), except values inside S3.EXE's image
    (0x400000..0x7ed000: globals, vtables, strings, functions; that part of
    the world is the middle of the tile array, never named by a constant)
    and constants declared as `std::uintptr_t` (addresses).

Allowed: src/s3re/game/layout.h and src/s3re/game/raw.h (the typed layout
and the one documented raw helper, RawAt). A line that genuinely needs raw
access can carry the marker `raw-ok` in a comment with the reason.

Usage: python tools/typed_access_lint.py   (exit code 1 on findings;
also run by `python tools/inventory.py check`).
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ROOT / "src" / "s3re"
ALLOWED = {"game/layout.h", "game/raw.h"}

WORLD_BEGIN, WORLD_END = 0x2D000, 0x153A128
IMAGE_BEGIN, IMAGE_END = 0x400000, 0x7ED000

OLD_HELPERS = re.compile(r"(?<![\w:])(At|WorldField)<|world_offset::")
WORLD_BYTE_CAST = re.compile(
    r"reinterpret_cast<\s*(const\s+)?(std::)?u?int8_t\s*\*\s*>\s*\(\s*(world|w|self->world)\s*\)")
CONSTANT = re.compile(r"constexpr\s+([\w:]+)\s+(k\w+)\s*=\s*(0x[0-9a-fA-F]+)")


def findings() -> list[str]:
    result = []
    for path in sorted(SOURCES.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        relative = path.relative_to(SOURCES).as_posix()
        if relative in ALLOWED:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if "raw-ok" in line:
                continue
            code = line.split("//", 1)[0]
            where = f"src/s3re/{relative}:{number}"
            if OLD_HELPERS.search(code):
                result.append(f"{where}: old raw accessor (use layout.h fields, or RawAt with a reason)")
            if WORLD_BYTE_CAST.search(code):
                result.append(f"{where}: byte-pointer cast of the world (use layout.h fields)")
            for match in CONSTANT.finditer(code):
                type_name, name, value = match.group(1), match.group(2), int(match.group(3), 16)
                if (WORLD_BEGIN <= value < WORLD_END and not IMAGE_BEGIN <= value < IMAGE_END
                        and type_name not in ("std::uintptr_t", "uintptr_t")):
                    result.append(f"{where}: {name} = {match.group(3)} looks like a world offset "
                                  "(use a layout.h field)")
    return result


def main() -> int:
    errors = findings()
    for error in errors:
        print(f"error: {error}")
    print(f"typed access lint: {len(errors)} finding(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
