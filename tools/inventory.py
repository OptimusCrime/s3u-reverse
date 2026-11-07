#!/usr/bin/env python3
"""Function inventory: validation, progress statistics and name export.

The inventory (re/functions.csv) is the project's record of what each function
in S3.EXE is and how far its reimplementation has come. Ghidra annotations
made through REA are lost when a session closes; this file is the permanent
copy and the source of truth.

Commands:
  check   Validate the inventory against the Ghidra baseline and the source
          code (hooks and tests). Exit code 1 on any error.
  stats   Show progress.
  names   Print a JSON list of {"address", "name"} for re-applying names to a
          fresh REA/Ghidra session (set_addresses_names).

Columns of re/functions.csv:
  address      0x-prefixed, 8 lowercase hex digits; rows sorted by address.
  name         Our name, e.g. WinMain or std::string::_Eos (Class::Method).
  category     game | crt | stl | mfc | watcom | compiler
                 game     = written by Blue Byte
                 crt      = Microsoft C runtime (statically linked), including
                            the old iostream library
                 stl      = Microsoft C++ standard library template code
                 mfc      = Microsoft Foundation Classes
                 watcom   = Watcom C++ container library (WC* classes)
                 compiler = other compiler-generated code (e.g. thunks)
  status       classified    = we know which class it belongs to and its role
                               there (vtable slot, destructor), not yet what
                               it does
               identified    = we know what it does and named it accordingly
               reimplemented = our replacement is hooked in, not yet verified
               verified      = differential tests prove it matches the original
  hook         none | passthrough (logs, then calls the original)
                    | replaced (our reimplementation runs instead)
  verified_by  Space-separated S3RE_TEST names. Required when verified.
  notes        Free text.
"""

import collections
import csv
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INVENTORY = ROOT / "re" / "functions.csv"
BASELINE = ROOT / "re" / "ghidra_functions.tsv"
EXTRA_FUNCTIONS = ROOT / "re" / "extra_functions.tsv"
SOURCE_DIR = ROOT / "src"

COLUMNS = ["address", "name", "category", "status", "hook", "verified_by", "notes"]
CATEGORIES = ("game", "crt", "stl", "mfc", "watcom", "compiler")
STATUSES = ("classified", "identified", "reimplemented", "verified")
HOOKS = ("none", "passthrough", "replaced")

ADDRESS_RE = re.compile(r"0x[0-9a-f]{8}")
NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(::~?[A-Za-z_][A-Za-z0-9_]*)*")
HOOK_RE = re.compile(r"S3RE_(HOOK|PASSTHROUGH)\(\s*(0x[0-9a-fA-F]+)\s*,\s*(\w+)")
TEST_RE = re.compile(r"S3RE_TEST\(\s*(\w+)\s*\)")


def load_baseline() -> dict[int, tuple[str, str]]:
    """All known function starts: Ghidra's list plus the starts the RTTI proves
    that Ghidra missed (kind "rtti"). Funclets are excluded."""
    functions = {}
    with BASELINE.open(encoding="utf-8") as f:
        lines = (line for line in f if not line.startswith("#"))
        for row in csv.DictReader(lines, delimiter="\t"):
            if row["kind"] != "funclet":
                functions[int(row["address"], 16)] = (row["kind"], row["ghidra_name"])
    if EXTRA_FUNCTIONS.exists():
        with EXTRA_FUNCTIONS.open(encoding="utf-8") as f:
            lines = (line for line in f if not line.startswith("#"))
            for row in csv.DictReader(lines, delimiter="\t"):
                address = int(row["address"], 16)
                functions.setdefault(address, (row["kind"], f"sub_{address:08x}"))
    return functions


def load_inventory() -> tuple[list[str], list[dict[str, str]]]:
    with INVENTORY.open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        return list(reader.fieldnames or []), list(reader)


def scan_sources() -> tuple[dict[int, tuple[str, str]], dict[str, str]]:
    """Returns ({address: (hook kind, file)}, {test name: file})."""
    hooks: dict[int, tuple[str, str]] = {}
    tests: dict[str, str] = {}
    for path in sorted(SOURCE_DIR.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        for kind, address, _name in HOOK_RE.findall(text):
            kind = "replaced" if kind == "HOOK" else "passthrough"
            hooks[int(address, 16)] = (kind, rel)
        for test in TEST_RE.findall(text):
            tests[test] = rel
    return hooks, tests


def check() -> int:
    errors: list[str] = []
    baseline = load_baseline()
    columns, rows = load_inventory()
    code_hooks, code_tests = scan_sources()

    if columns != COLUMNS:
        errors.append(f"header must be {','.join(COLUMNS)}")
        rows = []

    seen_names: dict[str, str] = {}
    previous = -1
    inventory_hooks: dict[int, str] = {}
    for line, row in enumerate(rows, start=2):
        where = f"functions.csv:{line}"
        address_text = row["address"]
        if not ADDRESS_RE.fullmatch(address_text):
            errors.append(f"{where}: address '{address_text}' must look like 0x0045a120")
            continue
        address = int(address_text, 16)
        if address <= previous:
            errors.append(f"{where}: rows must be sorted by address with no duplicates")
        previous = address
        if address not in baseline:
            errors.append(f"{where}: {address_text} is not a known function start "
                          "(re/ghidra_functions.tsv or re/extra_functions.tsv)")

        name = row["name"]
        if not NAME_RE.fullmatch(name):
            errors.append(f"{where}: invalid name '{name}'")
        elif name in seen_names:
            errors.append(f"{where}: name '{name}' also used at {seen_names[name]}")
        seen_names[name] = address_text

        for column, allowed in (("category", CATEGORIES), ("status", STATUSES),
                                ("hook", HOOKS)):
            if row[column] not in allowed:
                errors.append(f"{where}: {column} '{row[column]}' must be one of "
                              f"{', '.join(allowed)}")

        if row["status"] in ("reimplemented", "verified") and row["hook"] != "replaced":
            errors.append(f"{where}: status '{row['status']}' requires hook 'replaced'")

        tests = row["verified_by"].split()
        if row["status"] == "verified" and not tests:
            errors.append(f"{where}: status 'verified' requires verified_by test names")
        for test in tests:
            if test not in code_tests:
                errors.append(f"{where}: verified_by test '{test}' not found in src/")

        if row["hook"] != "none":
            inventory_hooks[address] = row["hook"]
            code = code_hooks.get(address)
            if code is None:
                errors.append(f"{where}: hook '{row['hook']}' but no S3RE_HOOK/"
                              f"S3RE_PASSTHROUGH for {address_text} in src/")
            elif code[0] != row["hook"]:
                errors.append(f"{where}: hook is '{row['hook']}' but {code[1]} installs "
                              f"a '{code[0]}' hook")

    for address, (kind, path) in sorted(code_hooks.items()):
        if address not in inventory_hooks:
            errors.append(f"{path}: {kind} hook at 0x{address:08x} has no inventory row "
                          "with a matching hook column")

    for error in errors:
        print(f"error: {error}")
    print(f"inventory check: {len(rows)} rows, {len(code_hooks)} hooks, "
          f"{len(code_tests)} tests, {len(errors)} error(s)")
    return 1 if errors else 0


def stats() -> int:
    baseline = load_baseline()
    _, rows = load_inventory()

    kinds = collections.Counter(kind for kind, _ in baseline.values())
    total = len(baseline)
    print(f"Known functions in S3.EXE (excluding exception funclets): {total}")
    print(f"  found by Ghidra: {total - kinds['rtti']} (library matches {kinds['library']}, "
          f"thunks {kinds['thunk']})")
    print(f"  missed by Ghidra, proven by RTTI: {kinds['rtti']}")
    print()

    by_category = collections.defaultdict(collections.Counter)
    for row in rows:
        by_category[row["category"]][row["status"]] += 1
    print(f"Inventory: {len(rows)} functions recorded")
    for category in CATEGORIES:
        counts = by_category.get(category)
        if counts:
            summary = ", ".join(f"{status} {counts[status]}" for status in STATUSES
                                if counts[status])
            print(f"  {category:<9} {summary}")
    print()

    def line(label, count):
        print(f"{label:<11}{count:5d} / {total} ({100 * count / total:.2f}%)")

    line("Named:", len(rows))
    line("Understood:", sum(1 for row in rows if row["status"] != "classified"))
    line("Verified:", sum(1 for row in rows if row["status"] == "verified"))
    return 0


def names() -> int:
    _, rows = load_inventory()
    print(json.dumps([{"address": row["address"], "name": row["name"]} for row in rows],
                     indent=1))
    return 0


def main() -> int:
    commands = {"check": check, "stats": stats, "names": names}
    if len(sys.argv) != 2 or sys.argv[1] not in commands:
        print(__doc__)
        return 2
    return commands[sys.argv[1]]()


if __name__ == "__main__":
    sys.exit(main())
